#import <Foundation/Foundation.h>

/// seroval: the wire format of TanStack Start server functions on ktk.education-system.ru.
@interface KTKSeroval : NSObject
/// seroval serializeString: the server unescapes every string node it receives.
+ (NSString *)escape:(NSString *)value;
/// seroval deserializeString: left to right, unknown sequences stay as they are.
+ (NSString *)unescape:(NSString *)value;
/// `{t: {data: {...fields}}, f: 127, m: []}`. `fields` is an ordered array of
/// @[key, value] pairs; an NSNumber value goes as a number, anything else as a string.
+ (NSDictionary *)requestWithFields:(NSArray *)fields;
/// Decode a seroval tree into plain JSON objects (NSNull for null); unwraps `{result}`.
+ (id)decode:(id)raw error:(NSError **)error;
@end
