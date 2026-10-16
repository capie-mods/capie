/** @file marker.cpp @brief Harmless symbol used to detect dyld insertion in owned test processes. */
/** @brief Identifies the fixture library. @return One. */
extern "C" int capie_fixture_loaded() {
    return 1;
}
