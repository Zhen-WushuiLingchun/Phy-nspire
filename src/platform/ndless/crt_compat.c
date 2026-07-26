/*
 * newlib init/fini compatibility for the Ndless runtime.
 *
 * Ndless runs C++ constructors and destructors from crt0, which walks
 * .init_array and .fini_array through __cpp_init and __cpp_fini. It therefore
 * supplies no _init or _fini, and neither does its crti.S/crtn.S.
 *
 * Prebuilt newlib distributions -- including the Arm GNU Toolchain release
 * pinned in research/upstreams.lock.json -- are configured with init/fini
 * support, so newlib's __libc_fini_array references _fini and the link fails
 * with "undefined reference to `_fini'". Ndless's own build_toolchain.sh
 * configures newlib differently and does not hit this.
 *
 * Empty bodies are the correct behaviour here, not a placeholder: the work
 * these functions would do is already done by crt0. They are weak so that a
 * toolchain supplying real ones wins.
 */

void _init(void);
void _fini(void);

__attribute__((weak)) void _init(void)
{
}

__attribute__((weak)) void _fini(void)
{
}
