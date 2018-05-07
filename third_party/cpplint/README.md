# CPP Lint
An extensible, privacy-preserving, user-data analysis pipeline.

* Downloaded from [Github](https://github.com/google/styleguide/tree/gh-pages/cpplint)

* Local Modifications
  * Require "COBALT_" as a prefix in the header guard

  * Allow `#include <condition_variable>`
  * Allow `#include <mutex>`
  * Allow `#include <thread>`
  * Allow `#include <chrono>`
  * Use the specified root flag to find the root of the repository.
