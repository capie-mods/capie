Compatibility headers that let the accepted offline suites run unchanged against the CAPIE
code base. Each header includes the new module and maps the old namespaces (`rn::...`)
or the old global names of the bridge onto the new ones. The only edits to the copied
suites in `../legacy/` are the include lines and file names (see the stage-2 report,
"Mapping of tests").
