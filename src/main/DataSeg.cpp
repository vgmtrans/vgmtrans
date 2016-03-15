#include "pch.h"
#include "DataSeg.h"


DataSeg::DataSeg(void)
    : startOff(0), size(0), endOff(0), bAlloced(false) {
}

DataSeg::~DataSeg(void) {
  clear();
}

void DataSeg::reposition(uint32_t newBegin) {
  if (newBegin < endOff && newBegin >= startOff) {
    uint32_t diff = newBegin - startOff;
    memmove(data, data + diff, size - diff);
  }
  else if (newBegin + size <= endOff && newBegin + size >= startOff) {
    uint32_t diff = endOff - (newBegin + size);
    memmove(data + diff, data, size - diff);
  }
  startOff = newBegin;
  endOff = startOff + size;
}

void DataSeg::alloc(uint32_t theSize) {
  assert(!bAlloced);

  if (bAlloced) {
    // alloc() does not save the existing data.
    delete[] data;
  }

  data = new uint8_t[theSize > 0 ? theSize : 1];
  size = theSize;
  bAlloced = true;
}

void DataSeg::load(uint8_t *buf, uint32_t startVirtOffset, uint32_t theSize) {
  clear();

  data = buf;
  startOff = startVirtOffset;
  size = theSize;
  endOff = startOff + size;
  bAlloced = true;
}


void DataSeg::clear() {
  if (bAlloced)
    delete[] data;
  //data.clear();
  startOff = 0;
  size = 0;
  endOff = 0;
  bAlloced = false;
}