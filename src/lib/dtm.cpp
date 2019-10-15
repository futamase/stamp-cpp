#include "dtm.hpp"

upcxx::dist_object<std::vector<std::vector<TxDescriptor>>> DTM::dist_desc_table({});

#define DTM_TEST

#ifdef DTM_TEST
int main() {
    DTM::Init(1);
    DTM::Exit();
}
#endif