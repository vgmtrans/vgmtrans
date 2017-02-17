#pragma once
#include "Format.h"
#include "Root.h"
#include "JaiSeqScanner.h"
#include "VGMColl.h"

// ************
// JaiSeqFormat
// ************

BEGIN_FORMAT(JaiSeqBMS)
USING_SCANNER(JaiSeqBMSScanner)
END_FORMAT()

BEGIN_FORMAT(JaiSeqAAF)
USING_SCANNER(JaiSeqAAFScanner)
END_FORMAT()

BEGIN_FORMAT(JaiSeqBAA)
USING_SCANNER(JaiSeqBAAScanner)
END_FORMAT()
