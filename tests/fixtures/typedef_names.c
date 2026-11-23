// A record or enumeration declared without a tag takes the name of the typedef that declares it.
typedef struct { float lo, hi; } range;
typedef struct {
    int nHsync;
    range hsync[8];
} /* the first declarator names the record */ MonRec, *MonPtr;
typedef struct { int a; } first_t, second_t;
struct { int c; } global_record;

void touch(MonPtr m, first_t *f, second_t *s) {
    m->hsync[m->nHsync].lo = 1;
    f->a = s->a + global_record.c;
}
typedef enum { CLR_RED, CLR_BLUE } colour_t;
colour_t paint(colour_t c) { return c == CLR_RED ? CLR_BLUE : CLR_RED; }
