/* Owned inert dependency. Its exported function is never called by this fixture. */
__attribute__((visibility("hidden"))) int fixture_dependency(void) { return 1; }
