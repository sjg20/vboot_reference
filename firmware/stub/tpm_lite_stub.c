/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Stub implementations of utility functions which call their linux-specific
 * equivalents.
 */

#include <assert.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "2common.h"
#include "2sysincludes.h"
#include "tlcl.h"
#include "tlcl_internal.h"

#define TPM_DEVICE_PATH "/dev/tpm0"
/* Retry failed open()s for 5 seconds in 10ms polling intervals. */
#define OPEN_RETRY_DELAY_NS (10 * 1000 * 1000)
#define OPEN_RETRY_MAX_NUM  500
#define COMM_RETRY_MAX_NUM  3

/* TODO: these functions should pass errors back rather than returning void */
/* TODO: if the only callers to these are just wrappers, should just
 * remove the wrappers and call us directly. */


/* The file descriptor for the TPM device.
 */
static int tpm_fd = -1;
/* If the library should exit during an OS-level TPM failure.
 */
static int exit_on_failure = 1;

static inline uint32_t try_exit(uint32_t result)
{
	if (exit_on_failure)
		exit(1);
	return result;
}

/* Print |n| bytes from array |a| to stderr, with newlines.
 */
__attribute__((unused)) static void DbgPrintBytes(const uint8_t* a, int n)
{
	int i;
	VB2_DEBUG_RAW("DEBUG: ");
	for (i = 0; i < n; i++) {
		if (i && i % 16 == 0)
			VB2_DEBUG_RAW("\nDEBUG: ");
		VB2_DEBUG_RAW("%02x ", a[i]);
	}
	VB2_DEBUG_RAW("\n");
}


/* Executes a command on the TPM.
 */
static uint32_t TpmExecute(const uint8_t *in, const uint32_t in_len,
			   uint8_t *out, uint32_t *pout_len)
{
	uint8_t response[TPM_MAX_COMMAND_SIZE];
	if (in_len <= 0) {
		VB2_DEBUG("ERROR: invalid command length %d for command %#x\n",
			  in_len, in[9]);
		return try_exit(TPM_E_INPUT_TOO_SMALL);
	} else if (tpm_fd < 0) {
		VB2_DEBUG("ERROR: the TPM device was not opened.  "
			  "Forgot to call TlclLibInit?\n");
		return try_exit(TPM_E_NO_DEVICE);
	} else {
		int n;
		int retries = 0;
		int first_errno = 0;

		/* Write command. Retry in case of communication errors.
		 */
		for ( ; retries < COMM_RETRY_MAX_NUM; ++retries) {
			n = write(tpm_fd, in, in_len);
			if (n >= 0) {
				break;
			}
			if (retries == 0) {
				first_errno = errno;
			}
			VB2_DEBUG("TPM: write attempt %d failed: %s\n",
				  retries + 1, strerror(errno));
		}
		if (n < 0) {
			VB2_DEBUG("ERROR: write failure to TPM device: %s "
				  "(first error %d)\n",
				  strerror(errno), first_errno);
			return try_exit(TPM_E_WRITE_FAILURE);
		} else if (n != in_len) {
			VB2_DEBUG("ERROR: bad write size to TPM device: "
				  "%d vs %u (%d retries, first error %d)\n",
				  n, in_len, retries, first_errno);
			return try_exit(TPM_E_WRITE_FAILURE);
		}

		/* Read response. Retry in case of communication errors.
		 */
		for (retries = 0, first_errno = 0;
		     retries < COMM_RETRY_MAX_NUM; ++retries) {
			n = read(tpm_fd, response, sizeof(response));
			if (n >= 0) {
				break;
			}
			if (retries == 0) {
				first_errno = errno;
			}
			VB2_DEBUG("TPM: read attempt %d failed: %s\n",
				  retries + 1, strerror(errno));
		}
		if (n == 0) {
			VB2_DEBUG("ERROR: null read from TPM device\n");
			return try_exit(TPM_E_READ_EMPTY);
		} else if (n < 0) {
			VB2_DEBUG("ERROR: read failure from TPM device: %s "
				  "(first error %d)\n",
				  strerror(errno), first_errno);
			return try_exit(TPM_E_READ_FAILURE);
		} else {
			if (n > *pout_len) {
				VB2_DEBUG("ERROR: TPM response too long for "
					  "output buffer\n");
				return try_exit(TPM_E_RESPONSE_TOO_LARGE);
			} else {
				*pout_len = n;
				memcpy(out, response, n);
			}
		}
	}
	return TPM_SUCCESS;
}

/* Gets the tag field of a TPM command.
 */
__attribute__((unused))
static inline int TpmTag(const uint8_t* buffer)
{
	uint16_t tag;
	FromTpmUint16(buffer, &tag);
	return (int) tag;
}

/* Gets the size field of a TPM command.
 */
__attribute__((unused))
static inline int TpmResponseSize(const uint8_t* buffer)
{
	uint32_t size;
	FromTpmUint32(buffer + sizeof(uint16_t), &size);
	return (int) size;
}

vb2_error_t vb2ex_tpm_init(void)
{
	char *no_exit = getenv("TPM_NO_EXIT");
	if (no_exit)
		exit_on_failure = !atoi(no_exit);
	return vb2ex_tpm_open();
}

vb2_error_t vb2ex_tpm_close(void)
{
	if (tpm_fd != -1) {
		close(tpm_fd);
		tpm_fd = -1;
	}
	return VB2_SUCCESS;
}

vb2_error_t vb2ex_tpm_open(void)
{
	const char *device_path;
	struct timespec delay;
	int retries, saved_errno;

	if (tpm_fd >= 0)
		return VB2_SUCCESS;  /* Already open */

	device_path = getenv("TPM_DEVICE_PATH");
	if (device_path == NULL) {
		device_path = TPM_DEVICE_PATH;
	}

	/* Retry TPM opens on EBUSY failures. */
	for (retries = 0; retries < OPEN_RETRY_MAX_NUM; ++ retries) {
		errno = 0;
		tpm_fd = open(device_path, O_RDWR | O_CLOEXEC);
		saved_errno = errno;
		if (tpm_fd >= 0)
			return VB2_SUCCESS;
		if (saved_errno != EBUSY)
			break;

		VB2_DEBUG("TPM: retrying %s: %s\n",
			  device_path, strerror(errno));

		/* Stall until TPM comes back. */
		delay.tv_sec = 0;
		delay.tv_nsec = OPEN_RETRY_DELAY_NS;
		nanosleep(&delay, NULL);
	}
	VB2_DEBUG("ERROR: TPM: Cannot open TPM device %s: %s\n",
		  device_path, strerror(saved_errno));
	return try_exit(VB2_ERROR_UNKNOWN);
}

uint32_t vb2ex_tpm_send_recv(const uint8_t* request, uint32_t request_length,
			     uint8_t* response, uint32_t* response_length)
{
	/*
	 * In a real firmware implementation, this function should contain
	 * the equivalent API call for the firmware TPM driver which takes a
	 * raw sequence of bytes as input command and a pointer to the
	 * output buffer for putting in the results.
	 *
	 * For EFI firmwares, this can make use of the EFI TPM driver as
	 * follows (based on page 16, of TCG EFI Protocol Specs Version 1.20
	 * availaible from the TCG website):
	 *
	 * EFI_STATUS status;
	 * status = TcgProtocol->EFI_TCG_PASS_THROUGH_TO_TPM(
	 *		TpmCommandSize(request),
	 *              request,
	 *              max_length,
	 *              response);
	 * // Error checking depending on the value of the status above
	 */
#ifndef NDEBUG
	int tag, response_tag;
#endif
	uint32_t result;

#ifdef VBOOT_DEBUG
	struct timeval before, after;
	VB2_DEBUG("request (%d bytes):\n", request_length);
	DbgPrintBytes(request, request_length);
	gettimeofday(&before, NULL);
#endif

	result = TpmExecute(request, request_length, response, response_length);
	if (result != TPM_SUCCESS)
		return result;

#ifdef VBOOT_DEBUG
	gettimeofday(&after, NULL);
	VB2_DEBUG("response (%d bytes):\n", *response_length);
	DbgPrintBytes(response, *response_length);
	VB2_DEBUG("execution time: %dms\n",
		  (int) ((after.tv_sec - before.tv_sec) * VB2_MSEC_PER_SEC +
			 (after.tv_usec - before.tv_usec) / VB2_USEC_PER_MSEC));
#endif

#ifndef NDEBUG
	/* validity checks */
	tag = TpmTag(request);
	response_tag = TpmTag(response);
	assert(
		(tag == TPM_TAG_RQU_COMMAND &&
		 response_tag == TPM_TAG_RSP_COMMAND) ||
		(tag == TPM_TAG_RQU_AUTH1_COMMAND &&
		 response_tag == TPM_TAG_RSP_AUTH1_COMMAND) ||
		(tag == TPM_TAG_RQU_AUTH2_COMMAND &&
		 response_tag == TPM_TAG_RSP_AUTH2_COMMAND));
	assert(*response_length == TpmResponseSize(response));
#endif

	return TPM_SUCCESS;
}

vb2_error_t vb2ex_tpm_get_random(uint8_t *buf, uint32_t length)
{
	static int urandom_fd = -1;
	if (urandom_fd < 0) {
		urandom_fd = open("/dev/urandom", O_RDONLY);
		if (urandom_fd == -1) {
			return VB2_ERROR_UNKNOWN;
		}
	}

	if (length != read(urandom_fd, buf, length)) {
		return VB2_ERROR_UNKNOWN;
	}

	return VB2_SUCCESS;
}
