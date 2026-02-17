#ifdef _WIN32
int wmain(int argc, wchar_t **argv);
int wmain(int argc, wchar_t **argv);
int main() { return 0; } // unused for windows build
#else
int main() { return 0; }
#endif
