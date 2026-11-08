#import <Foundation/Foundation.h>

extern NSString *const KTKErrorDomain;
/// NSNumber with the HTTP status of a KTKErrorHTTPStatus error.
extern NSString *const KTKHTTPStatusKey;

typedef NS_ENUM(NSInteger, KTKErrorCode) {
    KTKErrorNetwork = 1,   // no connection, TLS failure, timeout (message from curl)
    KTKErrorHTTPStatus,    // unexpected HTTP status
    KTKErrorTooLarge,      // response above the size limit
    KTKErrorBadResponse,   // not JSON / not the expected shape (often: the site was updated)
    KTKErrorLoginRequired, // the site rejected the saved session
    KTKErrorCredentials,   // the site rejected login and password; description = site message
    KTKErrorServer,        // other in-band error of a server function
    KTKErrorFile,          // local file system failure
};

NSError *KTKMakeError(KTKErrorCode code, NSString *message);

@interface KTKHTTPRequest : NSObject
@property (nonatomic, copy) NSString *method;      // GET (default) or POST
@property (nonatomic, strong) NSURL *URL;
@property (nonatomic, copy) NSDictionary *headers; // name -> value
@property (nonatomic, strong) NSData *body;        // POST only
@property (nonatomic) NSUInteger limit;            // max body bytes (default 4 MiB)
@property (nonatomic) NSTimeInterval timeout;      // whole request (default 60 s)
@property (nonatomic, copy) NSString *outputPath;  // stream the body into this file instead of memory
@end

@interface KTKHTTPResponse : NSObject
@property (nonatomic) NSInteger status;
@property (nonatomic, strong) NSData *body; // nil when written to outputPath
@property (nonatomic, copy) NSArray *setCookies; // raw Set-Cookie header values
@property (nonatomic, copy) NSString *location;
@end

/// libcurl + OpenSSL: TLS 1.2/1.3 with the bundled Mozilla CA list, HTTPS only, no automatic
/// redirects. Synchronous: never call on the main thread.
@interface KTKHTTP : NSObject
+ (KTKHTTPResponse *)perform:(KTKHTTPRequest *)request error:(NSError **)error;
/// Percent-encode everything except RFC 3986 unreserved characters.
+ (NSString *)escape:(NSString *)value;
@end
