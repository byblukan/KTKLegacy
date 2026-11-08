#import "KTKSeroval.h"
#import "KTKHTTP.h"

/// References copy earlier nodes, so a few kilobytes could expand into gigabytes.
/// Real weeks stay below 20 000 nodes.
static const NSUInteger KTKMaxSerovalNodes = 1 << 20;

/// Escapes in the order seroval's deserializeString matches them.
static NSString *const KTKEscaped[10] = {
    @"\\\\", @"\\\"", @"\\n", @"\\r", @"\\b", @"\\t", @"\\f", @"\\u2028", @"\\u2029", @"\\x3C",
};
static const unichar KTKRaw[10] = {'\\', '"', '\n', '\r', '\b', '\t', '\f', 0x2028, 0x2029, '<'};

@interface KTKSerovalDecoder : NSObject
@property (nonatomic, readonly) BOOL failed;
- (id)decode:(id)node;
@end

@implementation KTKSerovalDecoder {
    NSMutableDictionary *_byId; // node id -> @[value, expanded node count]
    NSUInteger _nodes;
}

- (instancetype)init {
    if ((self = [super init])) {
        _byId = [NSMutableDictionary dictionary];
    }
    return self;
}

- (BOOL)charge:(NSUInteger)count {
    _nodes += count;
    if (_nodes > KTKMaxSerovalNodes) {
        _failed = YES;
    }
    return !_failed;
}

- (id)decode:(id)node {
    if (_failed || ![self charge:1]) {
        return [NSNull null];
    }
    NSUInteger start = _nodes;
    if ([node isKindOfClass:[NSArray class]]) {
        return [self list:node];
    }
    if (![node isKindOfClass:[NSDictionary class]]) {
        return node ?: [NSNull null];
    }
    NSDictionary *map = node;
    NSNumber *nodeId = [map[@"i"] isKindOfClass:[NSNumber class]] ? map[@"i"] : nil;
    // Indexed values (t: 4) and repeated ids point at an already decoded node.
    if (nodeId) {
        NSArray *known = _byId[nodeId];
        if (known) {
            [self charge:[known[1] unsignedIntegerValue]];
            return known[0];
        }
    }
    id kind = map[@"t"];
    if (![kind isKindOfClass:[NSNumber class]]) {
        // Plain JSON around the tree, such as an unwrapped envelope.
        NSMutableDictionary *out = [NSMutableDictionary dictionaryWithCapacity:map.count];
        for (NSString *key in map) {
            out[key] = [self decode:map[key]];
        }
        return out;
    }
    id value;
    switch ([kind integerValue]) {
        case 1: { // string
            id s = map[@"s"];
            value = [s isKindOfClass:[NSString class]] ? [KTKSeroval unescape:s] : @"";
            break;
        }
        case 0:   // number
        case 3:   // bigint
        case 5:   // date
            value = map[@"s"] ?: [NSNull null];
            break;
        case 2: { // constant
            id s = map[@"s"];
            NSInteger constant = [s isKindOfClass:[NSNumber class]] ? [s integerValue] : -1;
            value = constant == 2 ? (id)@YES : constant == 3 ? (id)@NO : (id)[NSNull null];
            break;
        }
        case 7:   // set
        case 9: { // array
            id items = map[@"a"];
            value = [self list:[items isKindOfClass:[NSArray class]] ? items : @[]];
            break;
        }
        case 10:  // object
        case 11:  // null-prototype object
            value = [self properties:map[@"p"] keyNodes:NO];
            break;
        case 8:   // Map
            value = [self properties:map[@"e"] keyNodes:YES];
            break;
        default:  // promises, streams, symbols, dangling references: no schedule data
            value = [NSNull null];
            break;
    }
    if (nodeId) {
        _byId[nodeId] = @[value, @(_nodes - start + 1)];
    }
    return value;
}

- (NSArray *)list:(NSArray *)items {
    NSMutableArray *out = [NSMutableArray arrayWithCapacity:items.count];
    for (id item in items) {
        [out addObject:[self decode:item]];
        if (_failed) {
            break;
        }
    }
    return out;
}

- (NSDictionary *)properties:(id)record keyNodes:(BOOL)keyNodes {
    if (![record isKindOfClass:[NSDictionary class]]) {
        return @{};
    }
    NSArray *keys = record[@"k"];
    NSArray *values = record[@"v"];
    if (![keys isKindOfClass:[NSArray class]] || ![values isKindOfClass:[NSArray class]]) {
        return @{};
    }
    NSUInteger count = MIN(keys.count, values.count);
    NSMutableDictionary *out = [NSMutableDictionary dictionaryWithCapacity:count];
    for (NSUInteger i = 0; i < count && !_failed; i++) {
        NSString *key = nil;
        if (keyNodes) {
            id decoded = [self decode:keys[i]];
            if ([decoded isKindOfClass:[NSString class]]) {
                key = decoded;
            } else if ([decoded isKindOfClass:[NSNumber class]]) {
                key = [decoded stringValue];
            }
        } else if ([keys[i] isKindOfClass:[NSString class]]) {
            key = [KTKSeroval unescape:keys[i]];
        }
        if (key) {
            out[key] = [self decode:values[i]];
        }
    }
    return out;
}

@end

@implementation KTKSeroval

+ (NSString *)escape:(NSString *)value {
    NSUInteger length = value.length;
    NSMutableString *out = [NSMutableString stringWithCapacity:length];
    for (NSUInteger i = 0; i < length; i++) {
        unichar c = [value characterAtIndex:i];
        NSString *escaped = nil;
        for (int k = 0; k < 10; k++) {
            if (KTKRaw[k] == c) {
                escaped = KTKEscaped[k];
                break;
            }
        }
        if (escaped) {
            [out appendString:escaped];
        } else {
            [out appendString:[NSString stringWithCharacters:&c length:1]];
        }
    }
    return out;
}

+ (NSString *)unescape:(NSString *)value {
    if ([value rangeOfString:@"\\"].location == NSNotFound) {
        return value;
    }
    NSUInteger length = value.length;
    NSUInteger position = 0;
    NSMutableString *out = [NSMutableString stringWithCapacity:length];
    while (position < length) {
        NSRange slash = [value rangeOfString:@"\\" options:NSLiteralSearch
                                       range:NSMakeRange(position, length - position)];
        if (slash.location == NSNotFound) {
            [out appendString:[value substringFromIndex:position]];
            break;
        }
        [out appendString:[value substringWithRange:NSMakeRange(position, slash.location - position)]];
        BOOL matched = NO;
        for (int k = 0; k < 10; k++) {
            NSString *escaped = KTKEscaped[k];
            if (slash.location + escaped.length <= length &&
                [value compare:escaped options:NSLiteralSearch
                         range:NSMakeRange(slash.location, escaped.length)] == NSOrderedSame) {
                unichar raw = KTKRaw[k];
                [out appendString:[NSString stringWithCharacters:&raw length:1]];
                position = slash.location + escaped.length;
                matched = YES;
                break;
            }
        }
        if (!matched) {
            [out appendString:@"\\"];
            position = slash.location + 1;
        }
    }
    return out;
}

+ (NSDictionary *)requestWithFields:(NSArray *)fields {
    NSMutableArray *keys = [NSMutableArray arrayWithCapacity:fields.count];
    NSMutableArray *values = [NSMutableArray arrayWithCapacity:fields.count];
    for (NSArray *field in fields) {
        [keys addObject:[self escape:field[0]]];
        id value = field[1];
        if ([value isKindOfClass:[NSNumber class]]) {
            [values addObject:@{@"t": @0, @"s": value}];
        } else {
            [values addObject:@{@"t": @1, @"s": [self escape:[value description]]}];
        }
    }
    NSDictionary *data = @{@"t": @10, @"i": @1, @"p": @{@"k": keys, @"v": values}, @"o": @0};
    NSDictionary *root = @{@"t": @10, @"i": @0, @"p": @{@"k": @[@"data"], @"v": @[data]}, @"o": @0};
    return @{@"t": root, @"f": @127, @"m": @[]};
}

+ (id)decode:(id)raw error:(NSError **)error {
    id root = raw;
    if ([raw isKindOfClass:[NSDictionary class]] && [raw[@"t"] isKindOfClass:[NSDictionary class]] && raw[@"f"]) {
        root = raw[@"t"];
    }
    KTKSerovalDecoder *decoder = [KTKSerovalDecoder new];
    id value = [decoder decode:root];
    if (decoder.failed) {
        if (error) {
            *error = KTKMakeError(KTKErrorTooLarge, @"Ответ сайта слишком большой.");
        }
        return nil;
    }
    if ([value isKindOfClass:[NSDictionary class]] && value[@"result"]) {
        return value[@"result"];
    }
    return value;
}

@end
