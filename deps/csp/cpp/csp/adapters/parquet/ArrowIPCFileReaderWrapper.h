#ifndef _IN_CSP_ADAPTERS_PARQUET_ArrowIPCFileReaderWrapper_H
#define _IN_CSP_ADAPTERS_PARQUET_ArrowIPCFileReaderWrapper_H

#include <string>
#include <arrow/ipc/reader.h>
#include <csp/adapters/parquet/FileReaderWrapper.h>

namespace csp::adapters::parquet {
class ArrowIPCFileReaderWrapper : public FileReaderWrapper {
  public:
    ~ArrowIPCFileReaderWrapper();
    virtual void open(const std::string& fileName) override;
    virtual void close() override;

    bool readNextRowGroup(const std::vector<int> neededColumns,
                          std::shared_ptr<::arrow::Table>& dst) override;
    void getSchema(std::shared_ptr<::arrow::Schema>& dst) override;

  private:
    std::shared_ptr<::arrow::ipc::RecordBatchReader> m_fileReader;
};
}  // namespace csp::adapters::parquet

#endif
