const mp_obj_type_t pyb_saab_type = {
    {&mp_type_type },
    .name = MP_QSTR_SAAB,
    .print = saab_obj_print,
    .make_new = saab_obj_make_new
};

void saab_obj_print (void (*print)(void *env, const char *fmt, ...), void *env, mp_obj_t self_in, mp_print_kind_t kind) {
pyb_saab_obj_t *self = self_in;
print(env, "");
}

STATIC mp_obj_t saab_obj_make_new(mp_obj_t type_in, uint n_args, uint n_kw, const mp_obj_t *args) {
pyb_saab_obj_t *self = m_new_obj(pyb_saab_obj_t);
self->base.type = type_in;
return self;
}