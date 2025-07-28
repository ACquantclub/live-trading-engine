#include <string>
#include <csp/adapters/parquet/ParquetOutputAdapterManager.h>
#include <csp/adapters/parquet/ParquetOutputFilenameAdapter.h>

namespace csp::adapters::parquet {

void ParquetOutputFilenameAdapter::executeImpl() {
    m_parquetOutputAdapterManager.changeFileName(input()->lastValueTyped<std::string>());
}

}  // namespace csp::adapters::parquet
