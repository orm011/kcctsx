#include <stdlib.h>
#include <assert.h>

__attribute__((transaction_pure)) void __assert_fail(const char*, const char*, unsigned int, const char*);

class BaseVisitor {

public:
  virtual __attribute__((transaction_safe)) void visit(int *) = 0;
  virtual __attribute__((transaction_safe)) void visit2(int *) = 0;
};


class BaseAcceptor {

public:
  void accept1(){

    class DerivedVisitor : public BaseVisitor {
  
    public:
      virtual void visit(int *x) override {
	assert(*x >= 0);
	++(*x);
      }

      virtual void visit2(int *x) override {
	++(*x);
	++(*x);
      }
      
    } v;

    this->accept(&v);
  }

  void accept2(){
    class DerivedVisitor : public BaseVisitor {
  
    public:
      virtual void visit(int *x) override {
	assert(*x >= 0);
	++(*x);
	++(*x);
      }

      virtual void visit2(int *x) override {
	++(*x);
	++(*x);
	++(*x);
      }

    } v;

    this->accept(&v);
  }

  virtual void  accept(BaseVisitor *) =0;
  virtual void  accept_bulk(BaseVisitor *) = 0;
};


class DerivedAcceptor : public BaseAcceptor {
  int x = 0;
public:
  virtual void accept(BaseVisitor * v) {
  __transaction_atomic {
    assert(x >= 0);
    accept_impl(v);
  }
  }

  virtual void accept_bulk(BaseVisitor *v){
    __transaction_atomic {
      assert(x >= 0);
      accept_impl(v);
      accept_impl(v);
    }
  }

  void accept_impl(BaseVisitor *v){
    ++x;
    if (x > 10){
      v->visit(&x);
    } else {
      v->visit2(&x);
    }
  } 
};

int main() {

  BaseAcceptor *a = new DerivedAcceptor;

  for (int i = 0; i < 100000000; ++i){
    a->accept1();
  }
}
