// 
#ifndef DEBUG_H
#define DEBUG_H

// #define NO_LOGGING
// #define LOGGING_FORCED

#ifdef NO_LOGGING
#define LOG_IT(level, ...)
#else
#ifndef LOGGING_FORCED
#define LOG_IT(level, ...) XBMC->Log(level, __VA_ARGS__)
#else
#define LOG_IT(level, ...) XBMC->Log(LOG_ERROR, __VA_ARGS__)
#endif
#endif

#define DEBUGGING_XML 0
#if DEBUGGING_XML
void dump_to_log( TiXmlNode* pParent, unsigned int indent);
#else
#define dump_to_log(x, y)
#endif


#define DEBUGGING_API 0
#if DEBUGGING_API
#define LOG_API_CALL(f) LOG_IT(LOG_ERROR, "%s:  called!", f)
#define LOG_API_IRET(f,i) LOG_IT(LOG_ERROR, "%s: returns %d", f, i)
#else
#define LOG_API_CALL(f)
#define LOG_API_IRET(f,i)
#endif

#endif // DEBUG_H
