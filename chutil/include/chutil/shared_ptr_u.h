#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

namespace chutil {

class RC {
private:
  int count;

public:

  RC() { count=0; }

  void AddRef() {
    count++;
  }

  int Release() {
    if(count==0) return 0;
    return --count;
  }
};

template < typename T > class SP {
private:
  T *pData;       // pointer
  RC *reference; // Reference count

public:
  SP(): pData(0),reference(0) 
  {}

  SP(T *pValue): pData(pValue),reference(0) {
    reference=new RC();
    reference->AddRef();
  }

  SP(const SP<T> &sp): pData(sp.pData),reference(sp.reference) {
    reference->AddRef();
  }

  ~SP() {
      // Destructor
      // Decrement the reference count
      // if reference become zero delete the data
    if(reference->Release()==0) {
      delete pData;
      delete reference;
    }
  }

  void reset() {
    if(reference->Release()==0) {
      delete pData;
      delete reference;
      pData=0;
      reference=0;
    }

  }

  T &operator* () {
    return *pData;
  }

  const T &operator* () const  {
    return *pData;
  }

  T *operator-> () {
    return pData;
  }

  const T *operator-> () const {
    return pData;
  }

  operator T * () { return pData; }
  operator const T *() const { return pData; }
  operator T () { return *pData; }
  operator const T () const { return *pData; }

  SP<T> &operator = (const SP<T> &sp) {
    if(this!=&sp)
    {
      if(reference->Release()==0) {
        delete pData;
        delete reference;
      }
      pData=sp.pData;
      reference=sp.reference;
      reference->AddRef();
    }
    return *this;
  }

  T *get() { return pData; }
  const T *get() const { return pData; }

  //operator SP<const T> &() { return *this; }

};
}