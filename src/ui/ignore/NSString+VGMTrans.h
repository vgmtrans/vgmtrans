@interface NSString (VGMTrans)

+(NSString *)stringFromChar:(const char *)charText;
+(const char *)charFromString:(NSString *)string;
+(NSString *)stringFromWchar:(const wchar_t *)charText;
+(const wchar_t *)wcharFromString:(NSString *)string;

@end
