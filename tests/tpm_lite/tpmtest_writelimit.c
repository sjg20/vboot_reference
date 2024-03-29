/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Test of recovery when we hit the NVRAM write limit for an unowned TPM.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "2common.h"
#include "host_common.h"
#include "common/tests.h"
#include "tlcl.h"
#include "tlcl_tests.h"

#define TPM_MAX_NV_WRITES_NOOWNER 64

int main(int argc, char** argv) {
	int i;

	uint32_t result;

	TlclLibInit();

	TPM_CHECK(TlclStartupIfNeeded());
	TPM_CHECK(TlclSelfTestFull());
	TPM_CHECK(TlclAssertPhysicalPresence());
	TPM_CHECK(TlclForceClear());
	TPM_CHECK(TlclSetEnable());
	TPM_CHECK(TlclSetDeactivated(0));

	for (i = 0; i < TPM_MAX_NV_WRITES_NOOWNER + 2; i++) {
		printf("writing %d\n", i);
		if ((result = TlclWrite(INDEX0, (uint8_t*)&i,
					sizeof(i))) != TPM_SUCCESS) {
			switch (result) {
			case TPM_E_MAXNVWRITES:
				TEST_TRUE(i >= TPM_MAX_NV_WRITES_NOOWNER,
					  "MAXNVWRITES should only occur after "
					  "MAX_NV_WRITES_NOOWNER reached");
				break;
			default:
				VB2_DEBUG("unexpected error code %d (%#x)\n",
					  result, result);
				exit(1);
			}
		}
	}

	/* Reset write count */
	TPM_CHECK(TlclForceClear());
	TPM_CHECK(TlclSetEnable());
	TPM_CHECK(TlclSetDeactivated(0));

	/* Try writing again. */
	TPM_CHECK(TlclWrite(INDEX0, (uint8_t*)&i, sizeof(i)));

	printf("TEST SUCCEEDED\n");
	exit(0);
}
