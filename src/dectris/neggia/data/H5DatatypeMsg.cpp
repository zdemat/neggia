/**
MIT License

Copyright (c) 2017 DECTRIS Ltd.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "H5DatatypeMsg.h"
#include <assert.h>

H5DatatypeMsg::H5DatatypeMsg(const char *fileAddress, size_t offset):
   H5Object(fileAddress,offset)
{
    this->_init();
}

H5DatatypeMsg::H5DatatypeMsg(const H5Object & obj): H5Object(obj)
{
    this->_init();
}

unsigned int H5DatatypeMsg::version() const
{
   return (this->uint8(0) & 0xf0) >> 4;
}

unsigned int H5DatatypeMsg::typeId() const
{
   return this->uint8(0) & 0x0f;
}

unsigned int H5DatatypeMsg::dataSize() const
{
   return this->uint32(4);
}

bool H5DatatypeMsg::isSigned() const
{
   if(typeId() == 0) return this->uint8(1) & 0x8;
   else if (typeId() == 1) return true;
}

void H5DatatypeMsg::_init()
{
   unsigned int t = typeId();
   assert( t==0 || t==1);
}
