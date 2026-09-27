### analysis · tempo · mastering — allocation budgets in MSVC Debug

C ABI memory queries now cover MSVC iterator-debugging allocations as well as audio storage. Private prepared buffers use exact owned arrays; containers required by core APIs retain their iterator debugging and publish their construction costs. Mastering configuration keeps the original re-preparation and publishes its Debug temporaries, and a failed solution-record allocation reaches the ABI's poison handler in Debug too.

Windows CI now runs the four allocation-accounting ABI suites, container-construction controls, and five session suites in Debug; the job refuses a selection missing any of the ten suites. Release keeps the complete test run, including the session handle-generation walk.
