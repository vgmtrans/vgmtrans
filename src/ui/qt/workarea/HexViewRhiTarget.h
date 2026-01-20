/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
*/

#pragma once

class HexViewRhiTarget {
public:
  virtual ~HexViewRhiTarget() = default;

  virtual void markBaseDirty() = 0;
  virtual void markSelectionDirty() = 0;
  virtual void invalidateCache() = 0;
  virtual void requestUpdate() = 0;
};
