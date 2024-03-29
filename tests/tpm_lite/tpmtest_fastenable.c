/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Testing: ForceClear and behavior of disable and permanent deactivated flags.
 *
 * ForceClear sets the permanent disable and deactivated flags to their default
 * value of TRUE.  The specs say nothing about STCLEAR flags, so they should be
 * left alone.  This test checks that both flags may be reset without a reboot,
 * resulting in a fully enabled and activated TPM.  (We know that because
 * ForceClear requires that the TPM be enabled and activated to run.)
 */

#include <stdio.h>

#include "host_common.h"
#include "common/tests.h"
#include "tlcl.h"
#include "tlcl_tests.h"

int main(int argc, char** argv) {
	uint8_t disable, deactivated;
	int i;

	TlclLibInit();
	TPM_CHECK(TlclStartupIfNeeded());
	TPM_CHECK(TlclSelfTestFull());
	TPM_CHECK(TlclAssertPhysicalPresence());
	TPM_CHECK(TlclGetFlags(&disable, &deactivated, NULL));
	printf("disable is %d, deactivated is %d\n", disable, deactivated);

	for (i = 0; i < 2; i++) {
		TPM_CHECK(TlclForceClear());
		TPM_CHECK(TlclGetFlags(&disable, &deactivated, NULL));
		printf("disable is %d, deactivated is %d\n", disable, deactivated);
		TEST_EQ(disable, 1, "after ForceClear, disable");
		TEST_EQ(deactivated, 1, "after ForceClear, deactivated");
		TPM_CHECK(TlclSetEnable());
		TPM_CHECK(TlclSetDeactivated(0));
		TPM_CHECK(TlclGetFlags(&disable, &deactivated, NULL));
		printf("disable is %d, deactivated is %d\n", disable, deactivated);
		TEST_EQ(disable, 0, "after SetEnable, enabled");
		TEST_EQ(deactivated, 0, "after SetDeactivated(0), activated");
	}

	printf("TEST SUCCEEDED\n");
	return 0;
}
