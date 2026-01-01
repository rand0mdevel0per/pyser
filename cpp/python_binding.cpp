// python_binding.cpp
// Small C API wrappers to expose serialize/deserialize to Python.
// This file defines four functions exposed to Python:
// - serialize(obj) -> bytes
// - deserialize(bytes) -> object
// - serialize_to_file(obj, filename) -> None
// - deserialize_from_file(filename) -> object
// The module name is 'pyser' and is registered via PyModuleDef.

#include <Python.h>
#include "pyser.hpp"

static PyObject *py_serialize(PyObject *self, PyObject *args) {
    PyObject *obj;
    if (!PyArg_ParseTuple(args, "O", &obj)) {
        return nullptr;
    }
    try {
        pyser::PyObjectSerializer serializer;
        pyser::SerializedGraph graph = serializer.serialize(obj);

        std::vector<uint8_t> bytes = graph.to_bytes();

        return PyBytes_FromStringAndSize(
            reinterpret_cast<const char *>(bytes.data()),
            bytes.size()
        );
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}

static PyObject *py_deserialize(PyObject *self, PyObject *args) {
    PyObject *py_bytes;
    if (!PyArg_ParseTuple(args, "O", &py_bytes)) {
        return nullptr;
    }
    if (!PyBytes_Check(py_bytes)) {
        PyErr_SetString(PyExc_TypeError, "Expected bytes");
        return nullptr;
    }
    try {
        const char *data = PyBytes_AsString(py_bytes);
        Py_ssize_t size = PyBytes_Size(py_bytes);

        std::vector<uint8_t> bytes(data, data + size);
        pyser::SerializedGraph graph = pyser::SerializedGraph::from_bytes(bytes);

        pyser::PyObjectSerializer serializer;
        PyObject *res = serializer.deserialize(graph);
        if (PyErr_Occurred()) {
            Py_XDECREF(res);
            return nullptr;
        }
        return res;
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}

static PyObject *py_serialize_to_file(PyObject *self, PyObject *args) {
    PyObject *obj;
    const char *filename;

    if (!PyArg_ParseTuple(args, "Os", &obj, &filename)) {
        return nullptr;
    }
    try {
        pyser::PyObjectSerializer serializer;
        pyser::SerializedGraph graph = serializer.serialize(obj);

        std::vector<uint8_t> bytes = graph.to_bytes();
        FILE *fp = fopen(filename, "wb");
        if (!fp) {
            PyErr_SetFromErrno(PyExc_OSError);
            return nullptr;
        }
        size_t written = fwrite(bytes.data(), 1, bytes.size(), fp);
        fclose(fp);
        if (written != bytes.size()) {
            PyErr_SetString(PyExc_IOError, "Failed to write all data");
            return nullptr;
        }
        Py_RETURN_NONE;
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}

static PyObject *py_deserialize_from_file(PyObject *self, PyObject *args) {
    const char *filename;
    if (!PyArg_ParseTuple(args, "s", &filename)) {
        return nullptr;
    }
    try {
        FILE *fp = fopen(filename, "rb");
        if (!fp) {
            PyErr_SetFromErrno(PyExc_OSError);
            return nullptr;
        }
        fseek(fp, 0, SEEK_END);
        long file_size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        std::vector<uint8_t> bytes(file_size);
        size_t read_size = fread(bytes.data(), 1, file_size, fp);
        fclose(fp);
        if (read_size != static_cast<size_t>(file_size)) {
            PyErr_SetString(PyExc_IOError, "Failed to read all data");
            return nullptr;
        }
        pyser::SerializedGraph graph = pyser::SerializedGraph::from_bytes(bytes);
        pyser::PyObjectSerializer serializer;
        return serializer.deserialize(graph);
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}

static PyMethodDef methods[] = {
    {
        "serialize", py_serialize, METH_VARARGS,
        "Serialize Python object to bytes"
    },
    {
        "deserialize", py_deserialize, METH_VARARGS,
        "Deserialize Python object from bytes"
    },
    {
        "serialize_to_file", py_serialize_to_file, METH_VARARGS,
        "Serialize Python object and save to file"
    },
    {
        "deserialize_from_file", py_deserialize_from_file, METH_VARARGS,
        "Deserialize Python object from file"
    },
    {nullptr, nullptr, 0, nullptr}
};

static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT,
    "pyser",
    "High-performance Python object serialization library with chunking and checksums",
    -1,
    methods
};

PyMODINIT_FUNC PyInit_pyser(void) {
    return PyModule_Create(&module);
}
