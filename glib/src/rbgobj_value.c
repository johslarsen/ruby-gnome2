/* -*- c-file-style: "ruby"; indent-tabs-mode: nil -*- */
/**********************************************************************

  rbgobj_value.c -

  $Author: mutoh $
  $Date: 2006/05/14 10:04:04 $

  Copyright (C) 2002,2003  Masahiro Sakai

**********************************************************************/

#include "global.h"

/**********************************************************************/

static ID id_to_s;
static VALUE r2g_func_table;
static VALUE g2r_func_table;

void
rbgobj_register_r2g_func(GType gtype, RValueToGValueFunc func)
{
    VALUE obj = Data_Wrap_Struct(rb_cData, NULL, NULL, func);
    rb_hash_aset(r2g_func_table, INT2FIX(gtype), obj);
}

void
rbgobj_register_g2r_func(GType gtype, GValueToRValueFunc func)
{
    VALUE obj = Data_Wrap_Struct(rb_cData, NULL, NULL, func);
    rb_hash_aset(g2r_func_table, INT2FIX(gtype), obj);
}

/**********************************************************************/

VALUE
rbgobj_gvalue_to_rvalue(const GValue* value)
{
    if (!value)
        return Qnil;

    switch (G_TYPE_FUNDAMENTAL(G_VALUE_TYPE(value))) {
      case G_TYPE_NONE:
        return Qnil;
      case G_TYPE_CHAR:
        return CHR2FIX(g_value_get_char(value));
      case G_TYPE_UCHAR:
        return INT2FIX(g_value_get_uchar(value));
      case G_TYPE_BOOLEAN:
        return g_value_get_boolean(value) ? Qtrue : Qfalse;
      case G_TYPE_INT:
        return INT2NUM(g_value_get_int(value));
      case G_TYPE_UINT:
        return UINT2NUM(g_value_get_uint(value));
      case G_TYPE_LONG:
        return LONG2NUM(g_value_get_long(value));
      case G_TYPE_ULONG:
        return ULONG2NUM(g_value_get_ulong(value));
      case G_TYPE_INT64:
        return rbglib_int64_to_num(g_value_get_int64(value));
      case G_TYPE_UINT64:
        return rbglib_uint64_to_num(g_value_get_uint64(value));
      case G_TYPE_FLOAT:
        return rb_float_new(g_value_get_float(value));
      case G_TYPE_DOUBLE:
        return rb_float_new(g_value_get_double(value));
      case G_TYPE_STRING:
        {
            const char* str = g_value_get_string(value);
            return str ? rb_str_new2(str) : Qnil;
        }
      case G_TYPE_ENUM:
        return rbgobj_make_enum(g_value_get_enum(value), G_VALUE_TYPE(value));
      case G_TYPE_FLAGS:
        return rbgobj_make_flags(g_value_get_flags(value), G_VALUE_TYPE(value));
      case G_TYPE_OBJECT:
      case G_TYPE_INTERFACE:
        {
            GObject* gobj = g_value_get_object(value);
            return gobj ? GOBJ2RVAL(gobj) : Qnil;
        }
      case G_TYPE_PARAM:
        {
            GParamSpec* pspec = g_value_get_param(value);
            return pspec ? rbgobj_ruby_object_from_instance(pspec) : Qnil;
        }
      case G_TYPE_POINTER:
        {
            gpointer ptr = g_value_get_pointer(value);
            if (!ptr)
                return Qnil;
            else
                return rbgobj_ptr_new(G_VALUE_TYPE(value), ptr);
        }

      case G_TYPE_BOXED:
        {
            GType gtype;
            for (gtype = G_VALUE_TYPE(value);
                 gtype != G_TYPE_INVALID;
                 gtype = g_type_parent(gtype))
            {
                GValueToRValueFunc func;
                VALUE obj = rb_hash_aref(g2r_func_table, INT2NUM(gtype));
                if (NIL_P(obj))
                    continue;
                Data_Get_Struct(obj, void, func);
                return func(value);
            }
        }
      default:
        { 
          VALUE ret;
          
          ret = rbgobj_fund_gvalue2rvalue(
                          G_TYPE_FUNDAMENTAL(G_VALUE_TYPE(value)), 
                          value);
               
          if (NIL_P(ret))  {
             GValueToRValueFunc func;
             VALUE obj = rb_hash_aref(g2r_func_table, 
                 INT2NUM(G_TYPE_FUNDAMENTAL(G_VALUE_TYPE(value))));
             if (NIL_P(obj)) {
               g_warning("rbgobj_gvalue_to_rvalue: unsupported type: %s\n",
                  g_type_name(G_TYPE_FUNDAMENTAL(G_VALUE_TYPE(value))));
             } else {
               Data_Get_Struct(obj, void, func);
               ret = func(value);
             }
          }
          return ret;
        }
    }
}

void
rbgobj_rvalue_to_gvalue(VALUE val, GValue* result)
{
    switch (G_TYPE_FUNDAMENTAL(G_VALUE_TYPE(result))) {
      case G_TYPE_NONE:
        return;
      case G_TYPE_CHAR:
        g_value_set_char(result, NUM2INT(val));
        return;
      case G_TYPE_UCHAR:
        g_value_set_uchar(result, NUM2UINT(val));
        return;
      case G_TYPE_BOOLEAN:
        g_value_set_boolean(result, RTEST(val));
        return;
      case G_TYPE_INT:
        g_value_set_int(result, NUM2INT(val));
        return;
      case G_TYPE_UINT:
        g_value_set_uint(result, NUM2UINT(val));
        return;
      case G_TYPE_LONG:
        g_value_set_long(result, NUM2LONG(val));
        return;
      case G_TYPE_ULONG:
        g_value_set_ulong(result, NUM2ULONG(val));
        return;
      case G_TYPE_INT64:
        g_value_set_int64(result, rbglib_num_to_int64(val));
        return;
      case G_TYPE_UINT64:
        g_value_set_uint64(result, rbglib_num_to_uint64(val));
        return;
      case G_TYPE_ENUM:
        g_value_set_enum(result, rbgobj_get_enum(val, G_VALUE_TYPE(result)));
        return;
      case G_TYPE_FLAGS:
        g_value_set_flags(result, rbgobj_get_flags(val, G_VALUE_TYPE(result)));
        return;
      case G_TYPE_FLOAT:
        g_value_set_float(result, NUM2DBL(val));
        return;
      case G_TYPE_DOUBLE:
        g_value_set_double(result, NUM2DBL(val));
        return;
      case G_TYPE_STRING:
        {
            if (SYMBOL_P(val))
                val = rb_funcall(val, id_to_s, 0);
            g_value_set_string(result, NIL_P(val) ? NULL : StringValuePtr(val));
            return;
        }
      case G_TYPE_OBJECT:
      case G_TYPE_INTERFACE:
        g_value_set_object(result, NIL_P(val) ? NULL : RVAL2GOBJ(val));
        return;
      case G_TYPE_PARAM:
        g_value_set_param(result, NIL_P(val) ? NULL : rbgobj_param_spec_get_struct(val));
        return;
      case G_TYPE_POINTER:
        if (NIL_P(val))
            g_value_set_pointer(result, NULL);
        else
            g_value_set_pointer(result, rbgobj_ptr2cptr(val));
        return;

      case G_TYPE_BOXED:
        {
            GType gtype;
            for (gtype = G_VALUE_TYPE(result);
                 gtype != G_TYPE_INVALID;
                 gtype = g_type_parent(gtype))
            {
                RValueToGValueFunc func;
                VALUE obj = rb_hash_aref(r2g_func_table, INT2NUM(gtype));
                if (NIL_P(obj))
                    continue;
                Data_Get_Struct(obj, void, func);
                func(val, result);
                return;
            }
        }

      default:
        if (!rbgobj_fund_rvalue2gvalue(
                 G_TYPE_FUNDAMENTAL(G_VALUE_TYPE(result)),
                 val, result)) {
          RValueToGValueFunc func;
          VALUE obj = rb_hash_aref(r2g_func_table, 
              INT2NUM(G_TYPE_FUNDAMENTAL(G_VALUE_TYPE(result))));
          if (NIL_P(obj)) {
            g_warning("rbgobj_rvalue_to_gvalue: unsupported type: %s\n",
                g_type_name(G_VALUE_TYPE(result)));
          } else {
            Data_Get_Struct(obj, void, func);
            func(val, result);
          }
        }
    }
}

/**********************************************************************/

void Init_gobject_gvalue()
{
    id_to_s = rb_intern("to_s");
    r2g_func_table = rb_hash_new();
    g2r_func_table = rb_hash_new();
    rb_global_variable(&r2g_func_table);
    rb_global_variable(&g2r_func_table);
}
