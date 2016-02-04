/** ?? -*- C++ -*-
 * @file
 * @section License
 *
 * This file is part of Galois.  Galoisis a framework to exploit
 * amorphous data-parallelism in irregular programs.
 *
 * Galois is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, version 2.1 of the
 * License.
 *
 * Galois is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Galois.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * @section Copyright
 *
 * Copyright (C) 2015, The University of Texas at Austin. All rights
 * reserved.
 *
 */

#ifndef GALOIS_RUNTIME_ORDERED_LOCKABLE_H
#define GALOIS_RUNTIME_ORDERED_LOCKABLE_H

#include "Galois/AltBag.h"
#include "Galois/Runtime/ThreadRWlock.h"

#include <unordered_map>

namespace Galois {
namespace Runtime {

using dbg = Galois::Substrate::debug<0>;

template <typename T>
class OrderedContextBase: public SimpleRuntimeContext {
  using Base = SimpleRuntimeContext;

  T active;

public:

  explicit OrderedContextBase (const T& x): 
    Base (true), // call overriden subAcquire
    active (x)
  {}

  const T& getActive (void) const { return active; }

  // XXX: disable this. It will only work for modifications that don't change the priority
  T& getActive () { return active; }

  operator const T& (void) const { return getActive (); }

  operator T (void) const { return getActive (); }

  // XXX: disable this. It will only work for modifications that don't change the priority
  operator T& (void) const { return getActive (); }

};

// TODO: change comparator to three valued int instead of bool
template <typename Ctxt, typename Cmp>
struct ContextComparator {
  const Cmp& cmp;

  explicit ContextComparator (const Cmp& cmp): cmp (cmp) {}

  inline bool operator () (const Ctxt* left, const Ctxt* right) const {
    assert (left != NULL);
    assert (right != NULL);
    return cmp (left->getActive (), right->getActive ());
  }
};


template <typename NItem, typename Ctxt, typename CtxtCmp>
struct OrdLocFactoryBase {

  CtxtCmp ctxtcmp;

  explicit OrdLocFactoryBase (const CtxtCmp& ctxtcmp): ctxtcmp (ctxtcmp) {}

  void construct (NItem* ni, Lockable* l) const {
    assert (ni != nullptr);
    assert (l != nullptr);

    new (ni) NItem (l, ctxtcmp);
  }
};

template <typename NItem, typename Ctxt, typename CtxtCmp>
struct OrdLocBase: public LockManagerBase {

  using Base = LockManagerBase;

  using Factory = OrdLocFactoryBase<NItem, Ctxt, CtxtCmp>;

  Lockable* lockable;

  explicit OrdLocBase (Lockable* l): 
    Base (), lockable (l) {}

  bool tryMappingTo (Lockable* l) {
    return Base::CASowner (l, NULL);
  }

  void clearMapping () {
    // release requires having owned the lock
    bool r = Base::tryLock (lockable);
    assert (r);
    Base::release (lockable);
  }

  // just for debugging
  const Lockable* getMapping () const {
    return lockable;
  }

  static NItem* getOwner (Lockable* l) {
    LockManagerBase* o = LockManagerBase::getOwner (l);
    // assert (dynamic_cast<DAGnhoodItem*> (o) != nullptr);
    return static_cast<NItem*> (o);
  }
};

/**
 * NItem inherits from OrdLocBase publicly
 *
 * NItem contains nested type Factory
 *
 * Factory implements interface:
 *
 * NItem* create (Lockable* l);
 *
 * void destroy (NItem* ni);
 *
*/

template<typename NItem>
class PtrBasedNhoodMgr: boost::noncopyable {
public:
  typedef typename NItem::Factory NItemFactory;

  typedef FixedSizeAllocator<NItem> NItemAlloc;
  typedef Galois::PerThreadBag<NItem*> NItemWL;

protected:
  NItemAlloc niAlloc;
  NItemFactory& factory;
  NItemWL allNItems;

  NItem* create (Lockable* l) {
    NItem* ni = niAlloc.allocate (1);
    assert (ni != nullptr);
    factory.construct (ni, l);
    return ni;
  }

  void destroy (NItem* ni) {
    // delete ni; ni = NULL;
    niAlloc.destroy (ni);
    niAlloc.deallocate (ni, 1);
    ni = NULL;
  }

  
public:
  PtrBasedNhoodMgr(NItemFactory& f): factory (f) {}

  NItem& getNhoodItem (Lockable* l) {

    if (NItem::getOwner (l) == NULL) {
      // NItem* ni = new NItem (l, cmp);
      NItem* ni = create (l);

      if (ni->tryMappingTo (l)) {
        allNItems.get ().push_back (ni);

      } else {
        destroy (ni);
      }

      assert (NItem::getOwner (l) != NULL);
    }

    NItem* ret = NItem::getOwner (l);
    assert (ret != NULL);
    return *ret;
  }

  LocalRange<NItemWL> getAllRange (void) {
    return makeLocalRange (allNItems);
  }

  NItemWL& getContainer() {
    return allNItems;
  }

  ~PtrBasedNhoodMgr() {
    resetAllNItems();
  }

protected:
  struct Reset {
    PtrBasedNhoodMgr* self; 
    void operator()(NItem* ni) const {
      ni->clearMapping();
      self->destroy(ni);
    }
  };

  void resetAllNItems() {
    Reset fn {this};
    do_all_impl(makeLocalRange(allNItems), fn);
  }
};

template <typename NItem>
class MapBasedNhoodMgr: public PtrBasedNhoodMgr<NItem> {
public:
  typedef MapBasedNhoodMgr MyType;

  // typedef std::tr1::unordered_map<Lockable*, NItem> NhoodMap; 
  //
  typedef Pow_2_BlockAllocator<std::pair<Lockable*, NItem*> > MapAlloc;

  typedef std::unordered_map<
      Lockable*,
      NItem*,
      std::hash<Lockable*>,
      std::equal_to<Lockable*>,
      MapAlloc
    > NhoodMap;

  typedef Galois::Runtime::ThreadRWlock Lock_ty;
  typedef PtrBasedNhoodMgr<NItem> Base;

protected:
  NhoodMap nhoodMap;
  Lock_ty map_mutex;

public:

  MapBasedNhoodMgr (const typename Base::NItemFactory& f): 
    Base (f),
    nhoodMap (8, std::hash<Lockable*> (), std::equal_to<Lockable*> (), MapAlloc ())

  {}

  NItem& getNhoodItem (Lockable* l) {

    map_mutex.readLock ();
      typename NhoodMap::iterator i = nhoodMap.find (l);

      if (i == nhoodMap.end ()) {
        // create the missing entry

        map_mutex.readUnlock ();

        map_mutex.writeLock ();
          // check again to avoid over-writing existing entry
          if (nhoodMap.find (l) == nhoodMap.end ()) {
            NItem* ni = Base::factory.create (l);
            Base::allNItems.get ().push_back (ni);
            nhoodMap.insert (std::make_pair (l, ni));

          }
        map_mutex.writeUnlock ();

        // read again now
        map_mutex.readLock ();
        i = nhoodMap.find (l);
      }

    map_mutex.readUnlock ();
    assert (i != nhoodMap.end ());
    assert (i->second != nullptr);

    return *(i->second);
    
  }


  ~MapBasedNhoodMgr () {
    Base::resetAllNItems ();
  }

};

} // end namespace Runtime
} // end namespace Galois



#endif // GALOIS_RUNTIME_ORDERED_LOCKABLE_H
