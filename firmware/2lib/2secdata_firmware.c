/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Secure storage APIs
 */

#include "2sysincludes.h"
#include "2common.h"
#include "2crc8.h"
#include "2misc.h"
#include "2secdata.h"

vb2_error_t vb2api_secdata_firmware_check(struct vb2_context *ctx)
{
	struct vb2_secdata_firmware *sec =
		(struct vb2_secdata_firmware *)ctx->secdata_firmware;

	/* Verify CRC */
	if (sec->crc8 != vb2_crc8(sec, offsetof(struct vb2_secdata_firmware,
						crc8))) {
		VB2_DEBUG("secdata_firmware: bad CRC\n");
		return VB2_ERROR_SECDATA_FIRMWARE_CRC;
	}

	/* Verify version */
	if (sec->struct_version < VB2_SECDATA_FIRMWARE_VERSION) {
		VB2_DEBUG("secdata_firmware: version incompatible\n");
		return VB2_ERROR_SECDATA_FIRMWARE_VERSION;
	}

	return VB2_SUCCESS;
}

vb2_error_t vb2api_secdata_firmware_create(struct vb2_context *ctx)
{
	struct vb2_secdata_firmware *sec =
		(struct vb2_secdata_firmware *)ctx->secdata_firmware;

	/* Clear the entire struct */
	memset(sec, 0, sizeof(*sec));

	/* Set to current version */
	sec->struct_version = VB2_SECDATA_FIRMWARE_VERSION;

	/* Calculate initial CRC */
	sec->crc8 = vb2_crc8(sec, offsetof(struct vb2_secdata_firmware, crc8));

	/* Mark as changed */
	ctx->flags |= VB2_CONTEXT_SECDATA_FIRMWARE_CHANGED;

	return VB2_SUCCESS;
}

vb2_error_t vb2_secdata_firmware_init(struct vb2_context *ctx)
{
	struct vb2_shared_data *sd = vb2_get_sd(ctx);
	vb2_error_t rv;

	rv = vb2api_secdata_firmware_check(ctx);
	if (rv)
		return rv;

	/* Set status flag */
	sd->status |= VB2_SD_STATUS_SECDATA_FIRMWARE_INIT;

	/* Read this now to make sure crossystem has it even in rec mode. */
	rv = vb2_secdata_firmware_get(ctx, VB2_SECDATA_FIRMWARE_VERSIONS,
				      &sd->fw_version_secdata);
	if (rv)
		return rv;

	return VB2_SUCCESS;
}

vb2_error_t vb2_secdata_firmware_get(struct vb2_context *ctx,
				     enum vb2_secdata_firmware_param param,
				     uint32_t *dest)
{
	struct vb2_shared_data *sd = vb2_get_sd(ctx);
	struct vb2_secdata_firmware *sec =
		(struct vb2_secdata_firmware *)ctx->secdata_firmware;

	if (!(sd->status & VB2_SD_STATUS_SECDATA_FIRMWARE_INIT))
		return VB2_ERROR_SECDATA_FIRMWARE_GET_UNINITIALIZED;

	switch (param) {
	case VB2_SECDATA_FIRMWARE_FLAGS:
		*dest = sec->flags;
		return VB2_SUCCESS;

	case VB2_SECDATA_FIRMWARE_VERSIONS:
		*dest = sec->fw_versions;
		return VB2_SUCCESS;

	default:
		return VB2_ERROR_SECDATA_FIRMWARE_GET_PARAM;
	}
}

vb2_error_t vb2_secdata_firmware_set(struct vb2_context *ctx,
				     enum vb2_secdata_firmware_param param,
				     uint32_t value)
{
	struct vb2_secdata_firmware *sec =
		(struct vb2_secdata_firmware *)ctx->secdata_firmware;
	uint32_t now;

	if (!(vb2_get_sd(ctx)->status & VB2_SD_STATUS_SECDATA_FIRMWARE_INIT))
		return VB2_ERROR_SECDATA_FIRMWARE_SET_UNINITIALIZED;

	/* If not changing the value, don't regenerate the CRC. */
	if (vb2_secdata_firmware_get(ctx, param, &now) == VB2_SUCCESS &&
	    now == value)
		return VB2_SUCCESS;

	switch (param) {
	case VB2_SECDATA_FIRMWARE_FLAGS:
		/* Make sure flags is in valid range */
		if (value > 0xff)
			return VB2_ERROR_SECDATA_FIRMWARE_SET_FLAGS;

		VB2_DEBUG("secdata_firmware flags updated from 0x%x to 0x%x\n",
			  sec->flags, value);
		sec->flags = value;
		break;

	case VB2_SECDATA_FIRMWARE_VERSIONS:
		VB2_DEBUG("secdata_firmware versions updated from "
			  "0x%x to 0x%x\n",
			  sec->fw_versions, value);
		sec->fw_versions = value;
		break;

	default:
		return VB2_ERROR_SECDATA_FIRMWARE_SET_PARAM;
	}

	/* Regenerate CRC */
	sec->crc8 = vb2_crc8(sec, offsetof(struct vb2_secdata_firmware, crc8));
	ctx->flags |= VB2_CONTEXT_SECDATA_FIRMWARE_CHANGED;
	return VB2_SUCCESS;
}