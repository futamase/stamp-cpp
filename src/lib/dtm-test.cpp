#include "dtm.hpp"

dist_par_vec<TxDescriptor> DTM::desc_table;

int main(){
    DTM::Init(1);
    DTM::Exit();
}