#define PY_SSIZE_T_CLEAN
#include "domlette_interface.h"

/** Private Routines **************************************************/

static PyObject *newobj_function;
static PyObject *shared_empty_nodelist;
static PyObject *xml_base_key;
static PyObject *is_absolute_function;
static PyObject *absolutize_function;
static PyObject *deepcopy_function;

/** Public C API ******************************************************/

/* Allocates memory for a new node object of the given type and initializes
 * part of it.
 */
NodeObject *_Node_New(PyTypeObject *type)
{
  const size_t size = _PyObject_SIZE(type);
  PyObject *obj = _PyObject_GC_Malloc(size);
  if (obj == NULL)
    PyErr_NoMemory();
  else {
    memset(obj, '\0', size);
    PyObject_INIT(obj, type);
    _PyObject_GC_TRACK(obj);
  }
  return (NodeObject *)obj;
}

void _Node_Del(NodeObject *node)
{
  Py_CLEAR(node->parent);
  PyObject_GC_Del((PyObject *) node);
}

/* For debugging convenience. */
void _Node_Dump(char *msg, NodeObject *self)
{
  fprintf(stderr, "%s\n"
          "  node    : ", msg);
  if (self == NULL) {
    fprintf(stderr, "NULL\n");
  } else {
    PyObject_Print((PyObject *) self, stderr, 0);
    fprintf(stderr, "\n"
            "  type    : %s\n"
            "  refcount: %" PY_FORMAT_SIZE_T "d\n"
            "  parent  : %p\n",
            self->ob_type == NULL ? "NULL" : self->ob_type->tp_name,
            self->ob_refcnt,
            Node_GET_PARENT(self));
    if (Container_Check(self)) {
      fprintf(stderr, "  children: %" PY_FORMAT_SIZE_T "d\n",
              Container_GET_COUNT(self));
    }
  }
  fprintf(stderr, "----------------------\n");
}

/** Python Methods *****************************************************/

static char xml_select_doc[] = "xml_select(expr[, prefixes]) -> object\n\n\
Evaluates the XPath expression `expr` using this node as context.";

static PyObject *xml_select(NodeObject *self, PyObject *args, PyObject *kw)
{
  PyObject *expr, *explicit_nss = Py_None;
  PyObject *module, *result;
  static char *kwlist[] = { "expr", "prefixes", NULL };

  if (!PyArg_ParseTupleAndKeywords(args, kw, "O|O:xml_select", kwlist,
                                   &expr, &explicit_nss))
    return NULL;

  module = PyImport_ImportModule("amara.xpath.util");
  if (module == NULL) return NULL;
  result = PyObject_CallMethod(module, "SimpleEvaluate", "OOO",
                               expr, self, explicit_nss);
  Py_DECREF(module);
  return result;
}

static PyObject *node_copy(PyObject *self, PyObject *noargs)
{
  PyObject *info, *callable, *args, *state=NULL;
  PyObject *result;

  info = PyObject_CallMethod(self, "__reduce__", NULL);
  if (info == NULL)
    return NULL;
  if (!PyArg_UnpackTuple(info, NULL, 2, 3, &callable, &args, &state)) {
    Py_DECREF(info);
    return NULL;
  }
  Py_DECREF(info);

  result = PyObject_CallObject(callable, args);
  if (result == NULL)
    return NULL;
  if (state != NULL) {
    info = PyObject_CallMethod(result, "__setstate__", "O", state);
    if (info == NULL) {
      Py_DECREF(result);
      return NULL;
    }
    Py_DECREF(info);
  }
  return result;
}

static PyObject *node_deepcopy(PyObject *self, PyObject *memo)
{
  PyObject *info, *callable, *args, *state=NULL;
  PyObject *result;

  info = PyObject_CallMethod(self, "__reduce__", NULL);
  if (info == NULL)
    return NULL;
  if (!PyArg_UnpackTuple(info, NULL, 2, 3, &callable, &args, &state)) {
    Py_DECREF(info);
    return NULL;
  }
  Py_DECREF(info);

  args = PyObject_CallFunctionObjArgs(deepcopy_function, args, memo, NULL);
  if (args == NULL)
    return NULL;
  result = PyObject_CallObject(callable, args);
  Py_DECREF(args);
  if (result == NULL)
    return NULL;
  if (PyDict_SetItem(memo, self, result) < 0) {
    Py_DECREF(result);
    return NULL;
  }
  if (state != NULL) {
    state = PyObject_CallFunctionObjArgs(deepcopy_function, state, memo, NULL);
    if (state == NULL) {
      Py_DECREF(result);
      return NULL;
    }
    info = PyObject_CallMethod(result, "__setstate__", "O", state);
    if (info == NULL) {
      Py_DECREF(result);
      return NULL;
    }
    Py_DECREF(info);
  }
  return result;
}

static PyObject *node_reduce(PyObject *self, PyObject *noargs)
{
  PyObject *newargs, *args, *arg, *state, *info;
  Py_ssize_t i, n;

  newargs = PyObject_CallMethod(self, "__getnewargs__", NULL);
  if (newargs == NULL)
    return NULL;
  if (!PyTuple_Check(newargs)) {
    PyErr_Format(PyExc_TypeError,
                  "__getnewargs__() should return a tuple, not '%.200s'",
                  newargs == Py_None ? "None" : newargs->ob_type->tp_name);
    Py_DECREF(newargs);
    return NULL;
  }
  /* construct the argumnt list of (cls, *newargs) */
  n = PyTuple_GET_SIZE(newargs);
  args = PyTuple_New(n + 1);
  if (args == NULL) {
    Py_DECREF(newargs);
    return NULL;
  }
  i = 0;
  arg = (PyObject *)self->ob_type;
  do {
    PyTuple_SET_ITEM(args, i, arg);
    Py_INCREF(arg);
    if (i >= n)
      break;
    arg = PyTuple_GET_ITEM(newargs, i++);
  } while (1);
  Py_DECREF(newargs);

  state = PyObject_CallMethod(self, "__getstate__", NULL);
  if (state == NULL) {
    Py_DECREF(args);
    return NULL;
  }

  info = PyTuple_New(3);
  if (info == NULL) {
    Py_DECREF(args);
    Py_DECREF(state);
  } else {
    PyTuple_SET_ITEM(info, 0, newobj_function);
    Py_INCREF(newobj_function);
    PyTuple_SET_ITEM(info, 1, args);
    PyTuple_SET_ITEM(info, 2, state);
  }
  return info;
}

static PyObject *node_getnewargs(PyObject *self, PyObject *noarg)
{
  return PyTuple_New(0);
}

static PyObject *node_getstate(PyObject *self, PyObject *args)
{
  PyObject *deep=Py_True, *state;

  if (!PyArg_ParseTuple(args, "|O:__getstate__", &deep))
    return NULL;

  state = (PyObject *)Node_GET_PARENT(self);
  Py_INCREF(state);
  return state;
}

static PyObject *node_setstate(PyObject *self, PyObject *state)
{
  NodeObject *parent, *temp;

  if (Node_Check(state))
    parent = Node(state);
  else
    return PyErr_Format(PyExc_NotImplementedError,
                        "subclass '%s' must override __setstate__()",
                        self->ob_type->tp_name);

  temp = Node_GET_PARENT(self);
  Node_SET_PARENT(self, parent);
  Py_INCREF(parent);
  Py_XDECREF(temp);

  Py_INCREF(Py_None);
  return Py_None;
}

#define PyMethod_INIT(NAME, FLAGS) \
  { #NAME, (PyCFunction)NAME, FLAGS, NAME##_doc }

static PyMethodDef node_methods[] = {
  PyMethod_INIT(xml_select, METH_KEYWORDS),
  /* copy(), deepcopy(), pickle support */
  { "__copy__",       node_copy,       METH_NOARGS,  "helper for copy" },
  { "__deepcopy__",   node_deepcopy,   METH_O,       "helper for deepcopy" },
  { "__reduce__",     node_reduce,     METH_NOARGS,  "helper for pickle" },
  { "__getnewargs__", node_getnewargs, METH_NOARGS,  "helper for pickle" },
  { "__getstate__",   node_getstate,   METH_VARARGS, "helper for pickle" },
  { "__setstate__",   node_setstate,   METH_O,       "helper for pickle" },
  { NULL }
};

/** Python Members ****************************************************/

#define Node_MEMBER(NAME, MEMBER) \
  { #NAME, T_OBJECT, offsetof(NodeObject, MEMBER), RO }

static PyMemberDef node_members[] = {
  Node_MEMBER(xml_parent, parent),
  { NULL }
};

/** Python Computed Members ********************************************/

static PyObject *get_root(PyObject *self, void *arg)
{
  NodeObject *node = (NodeObject *)self;
  while (!Document_Check(node)) {
    node = Node_GET_PARENT(node);
    if (node == NULL) {
      Py_INCREF(Py_None);
      return Py_None;
    }
  }
  Py_INCREF(node);
  return (PyObject *)node;
}

static PyObject *get_base_uri(PyObject *self, void *arg)
{
  NodeObject *node = (NodeObject *)self;
  PyObject *base, *result;

  /* DOM3 baseURI is calculated according to XML Base */

  while (Node_GET_PARENT(node) != NULL) {
    /* 1. the base URI specified by an xml:base attribute on the element,
     *    if one exists, otherwise
     */
    if (Element_Check(node)) {
      base = PyDict_GetItem(Element_GET_ATTRIBUTES(node), xml_base_key);
      if (base) {
        base = Attr_GET_NODE_VALUE(base);
        /* If the xml:base in scope for the current node is not absolute, we find
         * the element where that xml:base was declared, then Absolutize our
         * relative xml:base against the base URI of the parent of declaring
         * element, recursively. */
        result = PyObject_CallFunction(is_absolute_function, "O", base);
        if (result == NULL) return NULL;
        switch (PyObject_IsTrue(result)) {
        case 0:
          Py_DECREF(result);
          result = get_base_uri((PyObject *)Node_GET_PARENT(node), arg);
          if (result == NULL) return NULL;
          else if (result == Py_None) return result;
          base = PyObject_CallFunction(absolutize_function, "OO", base, result);
          if (base == NULL) {
            Py_DECREF(result);
            return NULL;
          }
          /* fall through */
        case 1:
          Py_DECREF(result);
          Py_INCREF(base);
          return base;
        default:
          return NULL;
        }
      }
    }

    /* 2. the base URI of the element's parent element within the document
     *    or external entity, if one exists, otherwise
     */
    node = Node_GET_PARENT(node);
  }

  /* 3. the base URI of the document entity or external entity containing the
   *    element.
   */
  if (Document_Check(node)) {
    base = Document_GET_DOCUMENT_URI(node);
    result = PyObject_CallFunction(is_absolute_function, "O", base);
    if (result == NULL) return NULL;
    switch (PyObject_IsTrue(result)) {
    case 0:
      base = Py_None;
      /* fall through */
    case 1:
      break;
    default:
      return NULL;
    }
  } else {
    /* Node does not yet have a parent */
    base = Py_None;
  }

  Py_INCREF(base);
  return base;
}

static PyObject *get_preceding_sibling(PyObject *self, void *arg)
{
  NodeObject *parent;
  NodeObject **nodes;
  PyObject *sibling;
  Py_ssize_t count, index;

  parent = Node_GET_PARENT(self);
  if (parent == NULL) {
    Py_INCREF(Py_None);
    return Py_None;
  }
  assert(Container_Check(parent));

  nodes = Container_GET_NODES(parent);
  count = Container_GET_COUNT(parent);
  for (index = 0; index < count; index++) {
    if (nodes[index] == (NodeObject *)self) {
      if (index == 0) /* first child */
        sibling = Py_None;
      else
        sibling = (PyObject *)nodes[index - 1];
      Py_INCREF(sibling);
      return sibling;
    }
  }

  return DOMException_InvalidStateErr("lost from parent");
}

static PyObject *get_following_sibling(PyObject *self, void *arg)
{
  NodeObject *parent;
  NodeObject **nodes;
  PyObject *sibling;
  Py_ssize_t count, index;

  parent = Node_GET_PARENT(self);
  if (parent == NULL) {
    Py_INCREF(Py_None);
    return Py_None;
  }
  assert(Container_Check(parent));

  nodes = Container_GET_NODES(parent);
  count = Container_GET_COUNT(parent);
  for (index = 0; index < count; index++) {
    if (nodes[index] == (NodeObject *)self) {
      /* advance to the following node */
      index++;
      if (index == count) /* last child */
        sibling = Py_None;
      else
        sibling = (PyObject *)nodes[index];
      Py_INCREF(sibling);
      return sibling;
    }
  }

  return DOMException_InvalidStateErr("lost from parent");
}

static PyGetSetDef node_getset[] = {
  { "xml_root",               get_root },
  { "xml_base",               get_base_uri },
  { "xml_preceding_sibling",  get_preceding_sibling },
  { "xml_following_sibling",  get_following_sibling },
  { NULL }
};

/** Type Object ********************************************************/

static PyObject *node_repr(NodeObject *self)
{
  return PyString_FromFormat("<%s at %p>", self->ob_type->tp_name, self);
}

static int node_traverse(NodeObject *self, visitproc visit, void *arg)
{
  Py_VISIT(Node_GET_PARENT(self));
  return 0;
}

static int node_clear(NodeObject *self)
{
  Py_CLEAR(Node_GET_PARENT(self));
  return 0;
}

static long node_hash(NodeObject *self)
{
#if SIZEOF_LONG >= SIZEOF_VOID_P
  return (long)self;
#else
  /* convert to a Python long and hash that */
  PyObject *longobj;
  long hash;

  if ((longobj = PyLong_FromVoidPtr(self)) == NULL) {
    return -1;
  }

  hash = PyObject_Hash(longobj);
  Py_DECREF(longobj);
  return hash;
#endif
}

#define OPSTR(op) (op == Py_LT ? "Py_LT" : \
                   (op == Py_LE ? "Py_LE" : \
                    (op == Py_EQ ? "Py_EQ" : \
                     (op == Py_NE ? "Py_NE" : \
                      (op == Py_GE ? "Py_GE" : \
                       (op == Py_GT ? "Py_GT" : "?"))))))

#define BOOLSTR(ob) (ob == Py_True ? "Py_True" : \
                     (ob == Py_False ? "Py_False" : \
                      (ob == Py_NotImplemented ? "Py_NotImplemented" : \
                       (ob == NULL ? "NULL" : "?"))))

#define NODESTR(node) PyString_AS_STRING(PyObject_Repr(node))

static PyObject *node_richcompare(NodeObject *a, NodeObject *b, int op)
{
  PyObject *doc_a, *doc_b, *result;
  NodeObject *parent_a, *parent_b;
  int depth_a, depth_b;

  /* Make sure both arguments are cDomlette nodes */
  if (!(Node_Check(a) && Node_Check(b))) {
    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
  }

  if (a == b) {
    /* same objects */
    switch (op) {
    case Py_EQ: case Py_LE: case Py_GE:
      result = Py_True;
      break;
    case Py_NE: case Py_LT: case Py_GT:
      result = Py_False;
      break;
    default:
      result = Py_NotImplemented;
    }

    Py_INCREF(result);
    return result;
  }

  /* traverse to the top of each tree (document, element or the node itself)
  */
  parent_a = a;
  depth_a = 0;
  while (Node_GET_PARENT(parent_a)) {
    parent_a = Node_GET_PARENT(parent_a);
    depth_a++;
  }

  parent_b = b;
  depth_b = 0;
  while (Node_GET_PARENT(parent_b)) {
    parent_b = Node_GET_PARENT(parent_b);
    depth_b++;
  }

  /* compare the top of each tree; for Documents use the creation index,
   * otherwise None for trees not rooted in a Document. If both trees do
   * not have a Document root, fall back to default Python comparison. */
  doc_a = Document_Check(parent_a) ? Document_GET_INDEX(parent_a) : Py_None;
  doc_b = Document_Check(parent_b) ? Document_GET_INDEX(parent_b) : Py_None;
  if (doc_a != doc_b) {
    return PyObject_RichCompare(doc_a, doc_b, op);
  }
  else if (parent_a != parent_b) {
    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
  }

  /* if neither node is a document (depth>0), find the nodes common ancestor */
  if (depth_a > 0 && depth_b > 0) {
    NodeObject **nodes;
    Py_ssize_t i, count;

    /* traverse to the same depth in the tree for both nodes */
    for (i = depth_a; i > depth_b; i--) {
      a = Node_GET_PARENT(a);
    }

    for (i = depth_b; i > depth_a; i--) {
      b = Node_GET_PARENT(b);
    }

    /* find the nodes common parent */
    if (a != b) {
      parent_a = Node_GET_PARENT(a);
      parent_b = Node_GET_PARENT(b);
      while (parent_a != parent_b) {
        a = parent_a;
        parent_a = Node_GET_PARENT(parent_a);
        b = parent_b;
        parent_b = Node_GET_PARENT(parent_b);
      }

      /* get the nodes position in the child list */
      depth_a = depth_b = -1;
      nodes = Container_GET_NODES(parent_a);
      count = Container_GET_COUNT(parent_a);
      for (i = 0; i < count; i++) {
        if (nodes[i] == a)
          depth_a = i;
        else if (nodes[i] == b)
          depth_b = i;
      }
    }
  }

  switch (op) {
  case Py_LT:
    result = (depth_a < depth_b) ? Py_True : Py_False;
    break;
  case Py_LE:
    result = (depth_a <= depth_b) ? Py_True : Py_False;
    break;
  case Py_EQ:
    result = (depth_a == depth_b) ? Py_True : Py_False;
    break;
  case Py_NE:
    result = (depth_a != depth_b) ? Py_True : Py_False;
    break;
  case Py_GT:
    result = (depth_a > depth_b) ? Py_True : Py_False;
    break;
  case Py_GE:
      result = (depth_a >= depth_b) ? Py_True : Py_False;
    break;
  default:
    result = Py_NotImplemented;
  }

  /*
    PySys_WriteStdout("op: %s, a <%p>: %d, b <%p>: %d, result: %s\n",
                      OPSTR(op), a, depth_a, b, depth_b, BOOLSTR(result));
  */

  Py_INCREF(result);
  return result;
}

static char node_doc[] = "\
The Node type is the primary datatype for the entire Document Object Model.";

PyTypeObject DomletteNode_Type = {
  /* PyObject_HEAD     */ PyObject_HEAD_INIT(NULL)
  /* ob_size           */ 0,
  /* tp_name           */ Domlette_MODULE_NAME "." "Node",
  /* tp_basicsize      */ sizeof(NodeObject),
  /* tp_itemsize       */ 0,
  /* tp_dealloc        */ (destructor) _Node_Del,
  /* tp_print          */ (printfunc) 0,
  /* tp_getattr        */ (getattrfunc) 0,
  /* tp_setattr        */ (setattrfunc) 0,
  /* tp_compare        */ (cmpfunc) 0,
  /* tp_repr           */ (reprfunc) node_repr,
  /* tp_as_number      */ (PyNumberMethods *) 0,
  /* tp_as_sequence    */ (PySequenceMethods *) 0,
  /* tp_as_mapping     */ (PyMappingMethods *) 0,
  /* tp_hash           */ (hashfunc) node_hash,
  /* tp_call           */ (ternaryfunc) 0,
  /* tp_str            */ (reprfunc) 0,
  /* tp_getattro       */ (getattrofunc) 0,
  /* tp_setattro       */ (setattrofunc) 0,
  /* tp_as_buffer      */ (PyBufferProcs *) 0,
  /* tp_flags          */ (Py_TPFLAGS_DEFAULT |
                           Py_TPFLAGS_BASETYPE |
                           Py_TPFLAGS_HAVE_GC),
  /* tp_doc            */ (char *) node_doc,
  /* tp_traverse       */ (traverseproc) node_traverse,
  /* tp_clear          */ (inquiry) node_clear,
  /* tp_richcompare    */ (richcmpfunc) node_richcompare,
  /* tp_weaklistoffset */ 0,
  /* tp_iter           */ (getiterfunc) 0,
  /* tp_iternext       */ (iternextfunc) 0,
  /* tp_methods        */ (PyMethodDef *) node_methods,
  /* tp_members        */ (PyMemberDef *) node_members,
  /* tp_getset         */ (PyGetSetDef *) node_getset,
  /* tp_base           */ (PyTypeObject *) 0,
  /* tp_dict           */ (PyObject *) 0,
  /* tp_descr_get      */ (descrgetfunc) 0,
  /* tp_descr_set      */ (descrsetfunc) 0,
  /* tp_dictoffset     */ 0,
  /* tp_init           */ (initproc) 0,
  /* tp_alloc          */ (allocfunc) 0,
  /* tp_new            */ (newfunc) 0,
  /* tp_free           */ 0,
};

/** Module Interface **************************************************/

static PyObject *newobj_call(PyObject *module, PyObject *args)
{
  PyObject *cls, *newargs, *obj;
  PyTypeObject *type;
  Py_ssize_t len;

  assert(PyTuple_Check(args));
  len = PyTuple_GET_SIZE(args);
  if (len < 1) {
    PyErr_SetString(PyExc_TypeError, 
                    "__newobj__() takes at least 1 argument (0 given)");
    return NULL;
  }

  cls = PyTuple_GET_ITEM(args, 0);
  if (!PyType_Check(cls)) {
    PyErr_Format(PyExc_TypeError,
                 "__newobj__() argument 1 must be type, not %.50s",
                 cls == Py_None ? "None" : cls->ob_type->tp_name);
    return NULL;
  }

  type = (PyTypeObject *)cls;
  if (type->tp_new == NULL) {
    PyErr_Format(PyExc_TypeError, "type '%.100s' has NULL tp_new slot",
                 type->tp_name);
    return NULL;
  }

  /* create the argument tuple for the __new__ method */
  newargs = PyTuple_New(len - 1);
  if (newargs == NULL)
    return NULL;
  while (len > 1) {
    PyObject *item = PyTuple_GET_ITEM(args, len--);
    Py_INCREF(item);
    PyTuple_SET_ITEM(newargs, len, item);
  }

  /* call __new__ */
  obj = type->tp_new(type, newargs, NULL);
  Py_DECREF(newargs);
  return obj;
}

static PyMethodDef newobj_meth = {
  /* ml_name  */ "__newobj__",
  /* ml_meth  */ newobj_call,
  /* ml_flags */ METH_VARARGS,
  /* ml_doc   */ "helper for pickle",
};

int DomletteNode_Init(PyObject *module)
{
  PyObject *import, *dict, *value;

  dict = PyModule_GetDict(module);
  newobj_function = PyCFunction_NewEx(&newobj_meth, NULL,
                                      PyDict_GetItemString(dict, "__name__"));
  if (newobj_function == NULL)
    return -1;
  if (PyDict_SetItemString(dict, newobj_meth.ml_name, newobj_function) < 0) {
    Py_DECREF(newobj_function);
    return -1;
  }

  import = PyImport_ImportModule("amara.lib.iri");
  if (import == NULL) return -1;
  is_absolute_function = PyObject_GetAttrString(import, "is_absolute");
  if (is_absolute_function == NULL) {
    Py_DECREF(import);
    return -1;
  }
  absolutize_function = PyObject_GetAttrString(import, "absolutize");
  if (absolutize_function == NULL) {
    Py_DECREF(import);
    return -1;
  }
  Py_DECREF(import);

  import = PyImport_ImportModule("copy");
  if (import == NULL)
    return -1;
  deepcopy_function = PyObject_GetAttrString(import, "deepcopy");
  Py_DECREF(import);
  if (deepcopy_function == NULL)
    return -1;

  /* Initialize type objects */
  if (PyType_Ready(&DomletteNode_Type) < 0)
    return -1;
  /* Assign "class" constants */
  dict = DomletteNode_Type.tp_dict;
  value = PyString_FromString("node");
  if (value == NULL)
    return -1;
  if (PyDict_SetItemString(dict, "xml_type", value) < 0)
    return -1;
  Py_DECREF(value);

  shared_empty_nodelist = PyList_New(0);
  if (shared_empty_nodelist == NULL)
    return -1;

  xml_base_key = Py_BuildValue("(ss)", 
                               "http://www.w3.org/XML/1998/namespace", "base");
  if (xml_base_key == NULL)
    return -1;

  Py_INCREF(&DomletteNode_Type);
  return PyModule_AddObject(module, "Node", (PyObject*)&DomletteNode_Type);
}

void DomletteNode_Fini(void)
{
  Py_DECREF(newobj_function);
  Py_DECREF(shared_empty_nodelist);
  Py_DECREF(xml_base_key);
  Py_DECREF(is_absolute_function);
  Py_DECREF(absolutize_function);
  Py_DECREF(deepcopy_function);

  PyType_CLEAR(&DomletteNode_Type);
}
