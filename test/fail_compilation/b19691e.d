// REQUIRED_ARGS: -de
/* TEST_OUTPUT:
---
fail_compilation/b19691e.d(16): Error: constructor `b19691e.S2.this(S1 s = "")` is not callable using argument types `(string)`
fail_compilation/b19691e.d(16):        cannot pass argument `""` of type `string` to parameter `S1 s = ""`
fail_compilation/b19691e.d(22): Deprecation: constructor `b19691e.S2.this` all parameters have default arguments, but structs cannot have default constructors.
---
*/
// https://issues.dlang.org/show_bug.cgi?id=19691
module b19691e;

struct S1
{
    this(T)(T)
    {
        S2("");
    }
}

struct S2
{
    this(S1 s = ""){}
}
