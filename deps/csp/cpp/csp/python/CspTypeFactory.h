#ifndef _IN_CSP_PYTHON_CSPTYPEFACTORY_H
#define _IN_CSP_PYTHON_CSPTYPEFACTORY_H

#include <unordered_map>
#include <Python.h>
#include <csp/core/Platform.h>
#include <csp/engine/CspType.h>

namespace csp::python {

class CSPTYPESIMPL_EXPORT CspTypeFactory {
  public:
    static CspTypeFactory& instance();

    CspTypePtr& typeFromPyType(PyObject*);
    void removeCachedType(PyTypeObject*);

  private:
    using Cache = std::unordered_map<PyTypeObject*, CspTypePtr>;
    Cache m_cache;
};

}  // namespace csp::python

#endif
