#include <wchar.h>

@implementation NSString (VGMTrans)

+(NSString *)stringFromChar:(const char *)charText
{
    return [NSString stringWithUTF8String:charText];
}

+(const char *)charFromString:(NSString *)string
{
    return [string cStringUsingEncoding:NSUTF8StringEncoding];
}

+(NSString *)stringFromWchar:(const wchar_t *)charText
{
    return [[NSString alloc] initWithBytes:charText length:wcslen(charText)*sizeof(*charText) encoding:NSUTF32LittleEndianStringEncoding];
}

+(const wchar_t *)wcharFromString:(NSString *)string
{
    return  (const wchar_t *)[string cStringUsingEncoding:NSUTF32LittleEndianStringEncoding];
}

@end

