#pragma once
#include "Format.h"
#include "Root.h"
#include "JaiSeqScanner.h"
#include "VGMColl.h"

// ************
// JaiSeqFormat
// ************

BEGIN_FORMAT(JaiSeq)
USING_SCANNER(JaiSeqScanner)
END_FORMAT()
