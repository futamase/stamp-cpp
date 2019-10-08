#ifndef RBTREE_HPP
#define RBTREE_HPP

#include <functional>
#include <cassert>
#include "stm.hpp"

#define TX_LDA(addr)       TxLoad(addr)

#define DECLARE_TMFN(Name, ...) \
        Tx##Name(TM_ARGDECL __VA_ARGS__)
#define DECLARE_TMFN_ALONE(Name) \
        Tx##Name(TM_ARGDECL_ALONE)
#define CALL_TMFN(Name, ...) \
        Tx##Name(TM_ARG __VA_ARGS__)
#define CALL_TMFN_ALONE(Name) \
        Tx##Name(TM_ARG_ALONE)

#define P_OF(p) CALL_TMFN(ParentOf, p)
#define L_OF(p) CALL_TMFN(LeftOf, p)
#define R_OF(p) CALL_TMFN(RightOf, p)
#define C_OF(p) CALL_TMFN(ColorOf, p)
#define SET_C(n, c) CALL_TMFN(SetColor, n, c)

template<class KeyType, class DataType>
struct RBTree {
  struct node {
    KeyType key;
    DataType data;
    node* left = nullptr;
    node* right = nullptr;
    node* parent = nullptr;
    enum class Color {
      Black, Red
    };
    Color color;
  };
  node* root = nullptr;
  RBTree() {}
  ~RBTree() { 
    //FreeNode(root);
  }

  DataType DECLARE_TMFN(Get, KeyType key) {
    node* n = CALL_TMFN(Lookup, key); 
    if(n) {
      return TxLoad(&n->data);
    }
    return (DataType)0;
  }
  bool DECLARE_TMFN(Contains, KeyType key) {
    node* n = CALL_TMFN(Lookup, key); 
    return n;
  }
  bool DECLARE_TMFN(Insert, KeyType key, DataType value) {
    node* n = (node*)TxAlloc(sizeof(node));
    node* ex = CALL_TMFN(InsertNode, key, value, n);
    if(ex) CALL_TMFN(ReleaseNode, n);
    return (ex ? false : true);
  }
  bool DECLARE_TMFN(Delete, KeyType key) {
    node* n = CALL_TMFN(Lookup, key);
    if(n) {
      n = CALL_TMFN(DeleteNode, n);
      if(n) {
        CALL_TMFN(ReleaseNode, n);
      }
    }
    return n;
  }

  DataType Get(KeyType key) {
    node* n = LookUp(key);
    if(n) {
      return n->data;
    }
    return (DataType)0;
  }
  bool Contains(KeyType key) {
    return LookUp(key);
  }
  bool Insert(KeyType key, DataType value) {
    node* n = (node*)malloc(sizeof(node));
    node* ex = InsertNode(key, value, n);
    if(ex) ReleaseNode(n);
    return (ex ? false : true);
  }
  bool Delete(KeyType key) {
    node* n = LookUp(key);
    if(n) {
      n = DeleteNode(n);
    }
    if(n) {
      // ReleaseNode(n); なぜか失敗する
      free(n);
      n = nullptr;
    }
    return (n ? true : false);
  }

  private:
  node* LookUp(const KeyType& key) {
    node* p = this->root;
    while(p) {
      KeyType cmp = key - p->key;
      if(cmp == 0) 
        return p;
      p = (cmp < 0 ? p->left : p->right);
    }
    return nullptr;
  }
  void FreeNode(node* n) {
    if(n) {
      FreeNode(n->left);
      FreeNode(n->right);
      free(n);
    }
  }
  node* DeleteNode(node* p) {
    if(p->left && p->right) {
      node* s = Successor(p);
      p->key = s->key;
      p->data = s->data;
      p = s;
    }
    node* replacement = (p->left ? p->left : p->right);

    if(replacement) {
      replacement->parent = p->parent;
      node* pp = p->parent;
      if(!pp) this->root = replacement;
      else if(p == pp->left) pp->left = replacement;
      else pp->right = replacement;

      p->left = nullptr;
      p->right = nullptr;
      p->parent = nullptr;

      if(IsSameColor(p->color, node::Color::Black))
        FixAfterDeletion(replacement);
    } else if(!p->parent) {
      this->root = nullptr;
    } else {
      if(IsSameColor(p->color, node::Color::Black))
        FixAfterDeletion(p);
      node* pp = p->parent;
      if(pp) {
        if(p == pp->left) 
          pp->left = nullptr;
        else if(p == pp->right)
          pp->right = nullptr;
        p->parent = nullptr;
      }
    }
    return p;
  }
  void ReleaseNode(node* n) {
    free(n);
    n = nullptr;
  }
  node* Successor(node* t) {
    if(!t) return nullptr;
    else if(t->right) {
      node* p = t->right;
      while(p->left)
        p = p->left;
      return p;
    } else {
      node* p = t->parent;
      node* ch = t;
      while(p && ch == p->right) {
        ch = p;
        p = p->parent;
      }
      return p;
    }
  }
  void FixAfterInsertion(node* x) {
    x->color = node::Color::Red;
    while(x && x != root) {
      node* xp = x->parent;
      if(!IsSameColor(xp->color, node::Color::Red))
        break;
      if (ParentOf(x) == LeftOf(ParentOf(ParentOf(x)))) {
        node *y = RightOf(ParentOf(ParentOf(x)));
        if (IsSameColor(ColorOf(y), node::Color::Red)) {
          SetColor(ParentOf(x), node::Color::Black);
          SetColor(y, node::Color::Black);
          SetColor(ParentOf(ParentOf(x)), node::Color::Red);
          x = ParentOf(ParentOf(x));
        } else {
          if (x == RightOf(ParentOf(x))) {
            x = ParentOf(x);
            RotateLeft(x);
          }
          SetColor(ParentOf(x), node::Color::Black);
          SetColor(ParentOf(ParentOf(x)), node::Color::Red);
          if (ParentOf(ParentOf(x))) {
            RotateRight(ParentOf(ParentOf(x)));
          }
        }
      } else {
        node *y = LeftOf(ParentOf(ParentOf(x)));
        if (IsSameColor(ColorOf(y), node::Color::Red)) {
          SetColor(ParentOf(x), node::Color::Black);
          SetColor(y, node::Color::Black);
          SetColor(ParentOf(ParentOf(x)), node::Color::Red);
          x = ParentOf(ParentOf(x));
        } else {
          if (x == LeftOf(ParentOf(x))) {
            x = ParentOf(x);
            RotateRight(x);
          }
          SetColor(ParentOf(x), node::Color::Black);
          SetColor(ParentOf(ParentOf(x)), node::Color::Red);
          if (ParentOf(ParentOf(x))) {
            RotateLeft(ParentOf(ParentOf(x)));
          }
        }
      }
    }
    node *ro = root; 
    if (!IsSameColor(ro->color, node::Color::Black)) {
      SetColor(ro, node::Color::Black);
    }
  }
  void FixAfterDeletion(node* x) {
    while (x != root && IsSameColor(ColorOf(x), node::Color::Black)) {
      if (x == LeftOf(ParentOf(x))) {
        node *sib = RightOf(ParentOf(x));
        if (IsSameColor(ColorOf(sib), node::Color::Red)) {
          SetColor(sib, node::Color::Black);
          SetColor(ParentOf(x), node::Color::Red);
          RotateLeft(ParentOf(x));
          sib = RightOf(ParentOf(x));
        }
        if (IsSameColor(ColorOf(LeftOf(sib)), node::Color::Black) &&
            IsSameColor(ColorOf(RightOf(sib)), node::Color::Black)) {
          SetColor(sib, node::Color::Red);
          x = ParentOf(x);
        } else {
          if (IsSameColor(ColorOf(RightOf(sib)), node::Color::Black)) {
            SetColor(LeftOf(sib), node::Color::Black);
            SetColor(sib, node::Color::Red);
            RotateRight(sib);
            sib = RightOf(ParentOf(x));
          }
          SetColor(sib, ColorOf(ParentOf(x)));
          SetColor(ParentOf(x), node::Color::Black);
          SetColor(RightOf(sib), node::Color::Black);
          RotateLeft(ParentOf(x));
          x = root;
        }
      } else { /* symmetric */
        node* sib = LeftOf(ParentOf(x));
        if (IsSameColor(ColorOf(sib), node::Color::Red)) {
          SetColor(sib, node::Color::Black);
          SetColor(ParentOf(x), node::Color::Red);
          RotateRight(ParentOf(x));
          sib = LeftOf(ParentOf(x));
        }
        if (IsSameColor(ColorOf(RightOf(sib)), node::Color::Black) &&
            IsSameColor(ColorOf(LeftOf(sib)), node::Color::Black)) {
          SetColor(sib, node::Color::Red);
          x = ParentOf(x);
        } else {
          if (IsSameColor(ColorOf(LeftOf(sib)), node::Color::Black)) {
            SetColor(RightOf(sib), node::Color::Black);
            SetColor(sib, node::Color::Red);
            RotateLeft(sib);
            sib = LeftOf(ParentOf(x));
          }
          SetColor(sib, ColorOf(ParentOf(x)));
          SetColor(ParentOf(x), node::Color::Black);
          SetColor(LeftOf(sib), node::Color::Black);
          RotateRight(ParentOf(x));
          x = root;
        }
      }
    }

    if(x && !IsSameColor(ColorOf(x), node::Color::Black)) 
      SetColor(x, node::Color::Black);
  }
  void RotateLeft(node* x) {
    node* r = x->right;
    node* rl = r->left;
    x->right = rl;
    if(rl) rl->parent = x;
    node* xp = x->parent;
    r->parent = xp;
    if(!xp) root = r;
    else if(xp->left == x) xp->left = r;
    else xp->right = r;
    r->left = x;
    x->parent = r;
  }
  void RotateRight(node* x) {
    node* l = x->left;
    node* lr = l->right;
    x->left = lr;
    if(lr) lr->parent = x;
    node* xp = x->parent;
    l->parent = xp;
    if(!xp) root = l;
    else if(xp->right == x) xp->right = l;
    else xp->left = l;

    l->right = x;
    x->parent = l;
  }
  node* InsertNode(KeyType key, DataType value, node* n) {
    node* t = root;
    if(!t) {
      if(!n) return nullptr;

      n->left = nullptr;
      n->right = nullptr;
      n->parent = nullptr;
      n->key = key;
      n->data = value;
      n->color = node::Color::Black;
      root = n;
      return nullptr;
    }

    while(true) {
      auto cmp = key - t->key;
      if(cmp == 0) return t;
      else if(cmp < 0) {
        node* tl = t->left;
        if(tl) t = tl;
        else {
          n->left = nullptr;
          n->right = nullptr;
          n->key = key;
          n->data = value;
          n->parent = t;
          t->left = n;
          FixAfterInsertion(n);
          return nullptr;
        }
      } else { 
        node* tr = t->right;
        if(tr) t = tr;
        else {
          n->left = nullptr;
          n->right = nullptr;
          n->key = key;
          n->data = value;
          n->parent = t;
          t->right = n;
          FixAfterInsertion(n);
          return nullptr;
        }
      }
    }
  }


  node* DECLARE_TMFN(Lookup, const KeyType& key) {
    node* p = TxLoad(&root); 
    while(p) {
      KeyType cmp = key - TxLoad(&p->key);
      if(cmp == 0) 
        return p;
      p = (cmp < 0 ? TxLoad(&p->left) : TxLoad(&p->right));
    }

    return nullptr;
  }
  node* DECLARE_TMFN(DeleteNode, node* p) {
    if(TxLoad(&p->left) && TxLoad(&p->right)) {
      node* s = CALL_TMFN(Successor, p);
      TxStore(&p->key, TxLoad(&s->key));
      TxStore(&p->data, TxLoad(&s->data));
      p = s;
    }
    node* replacement = (TxLoad(&p->left) ? TxLoad(&p->left) : TxLoad(&p->right));
    if(replacement) {
      TxStore(&replacement->parent, TxLoad(&p->parent));
      node* pp = TxLoad(&p->parent);
      if(!pp) {
        TxStore(&this->root, replacement);
      } else if(p == TxLoad(&pp->left)) {
        TxStore(&pp->left, replacement);
      } else {
        TxStore(&pp->right, replacement);
      }
      TxStore(&p->left, (node*)0);
      TxStore(&p->right, (node*)0);
      TxStore(&p->parent, (node*)0);
      if(IsSameColor(TxLoad(&p->color), node::Color::Black))
        CALL_TMFN(FixAfterDeletion, replacement);
    } else if(!TxLoad(&p->parent)) {
      TxStore(&this->root, (node*)0);
    } else {
      if(IsSameColor(TxLoad(&p->color), node::Color::Black))
        CALL_TMFN(FixAfterDeletion, p);
      node* pp = TxLoad(&p->parent);
      if(pp) {
        if(p == TxLoad(&pp->left)) {
          TxStore(&pp->left, (node*)0);
        } else if(p == TxLoad(&pp->right)) {
          TxStore(&pp->right, (node*)0);
        }
        TxStore(&p->parent, (node*)0);
      }
    }
    return p;
  }
  void DECLARE_TMFN(ReleaseNode, node* n) {
    TxFree(n);
  }
  node* DECLARE_TMFN(Successor, node* n) {
    if(!n) return nullptr;

    if(TxLoad(&n->right)) {
      node* p = TxLoad(&n->right);
      while(TxLoad(&p->left)) {
        p = TxLoad(&p->left);
      }
      return p;
    } else {
      node* parent = TxLoad(&n->parent);
      node* child = n;
      while(parent && child == TxLoad(&parent->right)) {
        child = parent;
        parent = TxLoad(&parent->parent);
      }
      return parent;
    }
  }
  void DECLARE_TMFN(FixAfterInsertion, node* x) {
    TxStore(&x->color, node::Color::Red);
    while(x && x != TxLoad(&this->root)) {
      node* xp = TxLoad(&x->parent);
      if(!IsSameColor(C_OF(xp), node::Color::Red))
        break;
      if(P_OF(x) == L_OF(P_OF(P_OF(x)))) {
        node* y = R_OF(P_OF(P_OF(x)));
        if(IsSameColor(C_OF(y), node::Color::Red)) {
          SET_C(P_OF(x), node::Color::Black);
          SET_C(y, node::Color::Black);
          SET_C(P_OF(P_OF(x)), node::Color::Red);
          x = P_OF(P_OF(x));
        } else {
          if(x == R_OF(P_OF(x))) {
            x = P_OF(x);
            CALL_TMFN(RotateLeft, x);
          }
          SET_C(P_OF(x), node::Color::Black);
          SET_C(P_OF(P_OF(x)), node::Color::Red);
          if(P_OF(P_OF(x))) 
            CALL_TMFN(RotateRight, P_OF(P_OF(x)));
        }
      } else {
        node* y = L_OF((P_OF(P_OF(x))));
        if(IsSameColor(C_OF(y), node::Color::Red)) {
          SET_C(P_OF(x), node::Color::Black);
          SET_C(y, node::Color::Black);
          SET_C(P_OF(P_OF(x)), node::Color::Red);
          x = P_OF(P_OF(x));
        } else {
          if(x == L_OF(P_OF(x))) {
            x = P_OF(x);
            CALL_TMFN(RotateRight, x);
          }
          SET_C(P_OF(x), node::Color::Black);
          SET_C(P_OF(P_OF(x)), node::Color::Red);
          if(P_OF(P_OF(x)))
            CALL_TMFN(RotateLeft, P_OF(P_OF(x)));
        }
      }
    }
    node* ro = TxLoad(&this->root);
    if(!IsSameColor(TxLoad(&ro->color), node::Color::Black))
      TxStore(&ro->color, node::Color::Black);
  }
  void DECLARE_TMFN(FixAfterDeletion, node* x) {
    while(x != TxLoad(&this->root) && IsSameColor(C_OF(x), node::Color::Black)) {
      if(x == L_OF(P_OF(x))) {
        node* sib = R_OF(P_OF(x));
        if(IsSameColor(C_OF(sib), node::Color::Red)) {
          SET_C(sib, node::Color::Black);
          SET_C(P_OF(x), node::Color::Red);
          CALL_TMFN(RotateLeft, P_OF(x));
          sib = R_OF(P_OF(x));
        }
        if(IsSameColor(C_OF(L_OF(sib)), node::Color::Black) && 
           IsSameColor(C_OF(R_OF(sib)), node::Color::Black)) {
          SET_C(sib, node::Color::Red);
          x = P_OF(x);
        } else {
          if(IsSameColor(C_OF(R_OF(sib)), node::Color::Black)) {
            SET_C(L_OF(sib), node::Color::Black);
            SET_C(sib, node::Color::Red);
            CALL_TMFN(RotateRight, sib);
            sib = R_OF(P_OF(x));
          }
          SET_C(sib, C_OF(P_OF(x)));
          SET_C(P_OF(x), node::Color::Black);
          SET_C(R_OF(sib), node::Color::Black);
          CALL_TMFN(RotateLeft, P_OF(x));
          x = TxLoad(&this->root);
        }
      } else {
        node* sib = L_OF(P_OF(x));
        if(IsSameColor(C_OF(sib), node::Color::Red)) {
          SET_C(sib, node::Color::Black);
          SET_C(P_OF(x), node::Color::Red);
          CALL_TMFN(RotateRight, P_OF(x));
          sib = L_OF(P_OF(x));
        }
        if(IsSameColor(C_OF(R_OF(sib)), node::Color::Black) &&
           IsSameColor(C_OF(L_OF(sib)), node::Color::Black)) {
          SET_C(sib, node::Color::Red);
          x = P_OF(x);
        } else {
          if(IsSameColor(C_OF(L_OF(sib)), node::Color::Black)) {
            SET_C(R_OF(sib), node::Color::Black);
            SET_C(sib, node::Color::Red);
            CALL_TMFN(RotateLeft, sib);
            sib = L_OF(P_OF(x));
          }
          SET_C(sib, C_OF(P_OF(x)));
          SET_C(P_OF(x), node::Color::Black);
          SET_C(L_OF(sib), node::Color::Black);
          CALL_TMFN(RotateRight, P_OF(x));
          x = TxLoad(&this->root);
        }
      }
    }

    if(x && !IsSameColor(C_OF(x), node::Color::Black)) {
      TxStore(&x->color, node::Color::Black);
    }
  }
  void DECLARE_TMFN(RotateLeft, node* x) {
    node* r = TxLoad(&x->right);
    node* rl = TxLoad(&r->left);
    TxStore(&x->right, rl);
    if(rl) TxStore(&rl->parent, x);

    node* xp = TxLoad(&x->parent);
    TxStore(&r->parent, xp);
    if(!xp) TxStore(&this->root, r);
    else if(TxLoad(&xp->left) == x) TxStore(&xp->left, r);
    else TxStore(&xp->right, r);
    
    TxStore(&r->left, x);
    TxStore(&x->parent, r);
  }
  void DECLARE_TMFN(RotateRight, node* x) {
    node* l = TxLoad(&x->left);
    node* lr = TxLoad(&l->right);
    TxStore(&x->left, lr);
    if(lr) TxStore(&lr->parent, x);

    node* xp = TxLoad(&x->parent);
    TxStore(&l->parent, xp);
    if(!xp) TxStore(&this->root, l);
    else if(TxLoad(&xp->right) == x) TxStore(&xp->right, l);
    else TxStore(&xp->left, l);
    TxStore(&l->right, x);
    TxStore(&x->parent, l);
  }
  node* DECLARE_TMFN(InsertNode, KeyType key, DataType value, node* n) {
    node* t = TxLoad(&this->root);
    if(!t) {
      if(!n) return nullptr;
      TxStore(&n->left, (node*)0);
      TxStore(&n->right, (node*)0);
      TxStore(&n->parent, (node*)0);
      TxStore(&n->key, key);
      TxStore(&n->data, value);
      TxStore(&n->color, node::Color::Black);
      TxStore(&this->root, n);
      return nullptr;
    }

    while(true) {
      KeyType cmp = key - TxLoad(&t->key);
      if(cmp == 0) {
        return t;
      } else if(cmp < 0) {
        node* tl = TxLoad(&t->left);
        if(tl) t = tl;
        else {
          TxStore(&n->left, (node*)0);
          TxStore(&n->right, (node*)0);
          TxStore(&n->key, key);
          TxStore(&n->data, value);
          TxStore(&n->parent, t);
          TxStore(&t->left, n);
          CALL_TMFN(FixAfterInsertion, n);
          return nullptr;
        }
      } else {
        node* tr = TxLoad(&t->right);
        if(tr) t = tr;
        else {
          TxStore(&n->left, (node*)0);
          TxStore(&n->right, (node*)0);
          TxStore(&n->key, key);
          TxStore(&n->data, value);
          TxStore(&n->parent, t);
          TxStore(&t->right, n);
          CALL_TMFN(FixAfterInsertion, n);
          return nullptr;
        }
      }
    }
  }
  node* DECLARE_TMFN(ParentOf, node* n) const { return (n ? TxLoad(&n->parent) : nullptr); }
  node* DECLARE_TMFN(LeftOf, node* n) const { return (n ? TxLoad(&n->left) : nullptr); }
  node* DECLARE_TMFN(RightOf, node* n) const { return (n ? TxLoad(&n->right) : nullptr); }
  typename node::Color DECLARE_TMFN(ColorOf, node* n) const { return (n ? TxLoad(&n->color) : node::Color::Black); }
  void DECLARE_TMFN(SetColor, node* n, typename node::Color c) { if(n) TxStore(&n->color, c); }

  node* ParentOf(node* n) const { return (n ? n->parent : nullptr); }
  node* LeftOf(node* n) const { return (n ? n->left : nullptr); }
  node* RightOf(node* n) const { return (n ? n->right : nullptr); }
  typename node::Color ColorOf(node* n) const { return (n ? n->color : node::Color::Black); }
  void SetColor(node* n, typename node::Color c) { if(n) n->color = c; }
  bool IsSameColor(typename node::Color l, typename node::Color r) const {
    return static_cast<int>(l) == static_cast<int>(r);
  }
};

#define MAP_T                       RBTree<long, void*>
#define MAP_ALLOC(hash, cmp)        new RBTree<long, void*>()
#define MAP_FREE(map)               delete map

#define MAP_FIND(map, key)          map->Get(key)//rbtree_get(map, (void*)(key))
#define MAP_CONTAINS(map, key)      map->Contains(key)//rbtree_contains(map, (void*)(key))
#define MAP_INSERT(map, key, data)  map->Insert(key, data)//rbtree_insert(map, (void*)(key), (void*)(data))
#define MAP_REMOVE(map, key)        map->Delete(key)//rbtree_delete(map, (void*)(key))

#define TMMAP_FIND(map, key)        map->CALL_TMFN(Get, key) 
#define TMMAP_CONTAINS(map, key)    map->CALL_TMFN(Contains, key)
#define TMMAP_INSERT(map, key, data)map->CALL_TMFN(Insert, key, data)
#define TMMAP_REMOVE(map, key)      map->CALL_TMFN(Delete, key)

#endif