#ifdef _WIN64
  #ifdef _DEBUG
    #define TRIAGENS_VERSION "1.2.3 [WIN64-DEBUG]"
  #else  
    #define TRIAGENS_VERSION "1.2.3 [WIN64-RELEASE]"
  #endif  
#else  
  #ifdef _DEBUG
    #define TRIAGENS_VERSION "1.2.3 [WIN32-DEBUG]"
  #else  
    #define TRIAGENS_VERSION "1.2.3 [WIN32-RELEASE"
  #endif  
#endif  
