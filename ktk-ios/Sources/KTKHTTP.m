#import "KTKHTTP.h"
#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

NSString *const KTKErrorDomain = @"KTKErrorDomain";
NSString *const KTKHTTPStatusKey = @"KTKHTTPStatus";

NSError *KTKMakeError(KTKErrorCode code, NSString *message) {
    NSDictionary *info = message.length ? @{NSLocalizedDescriptionKey: message} : nil;
    return [NSError errorWithDomain:KTKErrorDomain code:code userInfo:info];
}

@implementation KTKHTTPRequest
@end

@implementation KTKHTTPResponse
@end

#pragma mark - curl callbacks

typedef struct {
    CFMutableDataRef data;
    FILE *file;
    size_t size;
    size_t limit;
    int overflow;
    int failed;
} KTKSink;

static size_t KTKWrite(char *ptr, size_t size, size_t count, void *userdata) {
    KTKSink *sink = userdata;
    size_t length = size * count;
    if (length > sink->limit - sink->size) {
        sink->overflow = 1;
        return 0;
    }
    sink->size += length;
    if (sink->file) {
        if (fwrite(ptr, 1, length, sink->file) != length) {
            sink->failed = 1;
            return 0;
        }
    } else {
        CFDataAppendBytes(sink->data, (const UInt8 *)ptr, (CFIndex)length);
    }
    return length;
}

static size_t KTKHeader(char *buffer, size_t size, size_t count, void *userdata) {
    size_t length = size * count;
    NSString *line = [[NSString alloc] initWithBytes:buffer length:length encoding:NSUTF8StringEncoding];
    if (!line) {
        line = [[NSString alloc] initWithBytes:buffer length:length encoding:NSISOLatin1StringEncoding];
    }
    if (line) {
        [(__bridge NSMutableArray *)userdata addObject:line];
    }
    return length;
}

#pragma mark - shared connection cache

// One share object keeps TLS sessions, DNS and open connections between requests, so a
// week with its tasks does not pay a full handshake per request on an old CPU.
static CURLSH *KTKShare;
static pthread_mutex_t KTKShareLocks[CURL_LOCK_DATA_LAST];

static void KTKLock(CURL *handle, curl_lock_data data, curl_lock_access access, void *userptr) {
    (void)handle; (void)access; (void)userptr;
    pthread_mutex_lock(&KTKShareLocks[data]);
}

static void KTKUnlock(CURL *handle, curl_lock_data data, void *userptr) {
    (void)handle; (void)userptr;
    pthread_mutex_unlock(&KTKShareLocks[data]);
}

@implementation KTKHTTP

+ (void)initialize {
    if (self != [KTKHTTP class]) {
        return;
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    for (int i = 0; i < CURL_LOCK_DATA_LAST; i++) {
        pthread_mutex_init(&KTKShareLocks[i], NULL);
    }
    KTKShare = curl_share_init();
    if (KTKShare) {
        curl_share_setopt(KTKShare, CURLSHOPT_LOCKFUNC, KTKLock);
        curl_share_setopt(KTKShare, CURLSHOPT_UNLOCKFUNC, KTKUnlock);
        curl_share_setopt(KTKShare, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        curl_share_setopt(KTKShare, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
        curl_share_setopt(KTKShare, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
    }
}

+ (NSString *)escape:(NSString *)value {
    const char *utf8 = value.UTF8String;
    char *escaped = curl_easy_escape(NULL, utf8, (int)strlen(utf8));
    if (!escaped) {
        return @"";
    }
    NSString *result = [NSString stringWithUTF8String:escaped];
    curl_free(escaped);
    return result ?: @"";
}

+ (KTKHTTPResponse *)perform:(KTKHTTPRequest *)request error:(NSError **)error {
    NSError *failure = nil;
    KTKHTTPResponse *response = [self performOnce:request error:&failure];
    if (!response && error) {
        *error = failure;
    }
    return response;
}

+ (KTKHTTPResponse *)performOnce:(KTKHTTPRequest *)request error:(NSError **)error {
    NSString *caPath = [[NSBundle mainBundle] pathForResource:@"cacert" ofType:@"pem"];
    if (!caPath || !request.URL) {
        *error = KTKMakeError(KTKErrorNetwork, @"Нет файла сертификатов или адреса запроса.");
        return nil;
    }
    CURL *curl = curl_easy_init();
    if (!curl) {
        *error = KTKMakeError(KTKErrorNetwork, @"curl_easy_init failed");
        return nil;
    }

    NSMutableData *data = [NSMutableData data];
    NSMutableArray *headerLines = [NSMutableArray array];
    KTKSink sink = {(__bridge CFMutableDataRef)data, NULL, 0, request.limit ?: 4 * 1024 * 1024, 0, 0};
    if (request.outputPath) {
        sink.file = fopen(request.outputPath.fileSystemRepresentation, "wb");
        if (!sink.file) {
            curl_easy_cleanup(curl);
            *error = KTKMakeError(KTKErrorFile, @"Не удалось создать файл.");
            return nil;
        }
    }

    struct curl_slist *headers = NULL;
    for (NSString *name in request.headers) {
        NSString *line = [NSString stringWithFormat:@"%@: %@", name, request.headers[name]];
        headers = curl_slist_append(headers, line.UTF8String);
    }
    // curl adds "Expect: 100-continue" to larger POSTs; the site does not need it.
    headers = curl_slist_append(headers, "Expect:");

    char errorBuffer[CURL_ERROR_SIZE];
    errorBuffer[0] = 0;

    curl_easy_setopt(curl, CURLOPT_URL, request.URL.absoluteString.UTF8String);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_CAINFO, caPath.fileSystemRepresentation);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)(request.timeout > 0 ? request.timeout : 60));
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, KTKWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, KTKHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, (__bridge void *)headerLines);
    if (KTKShare) {
        curl_easy_setopt(curl, CURLOPT_SHARE, KTKShare);
    }
    if ([request.method isEqualToString:@"POST"]) {
        NSData *body = request.body ?: [NSData data];
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body.length);
        curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, body.length ? body.bytes : "");
    }

    CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    if (sink.file) {
        if (fclose(sink.file) != 0) {
            sink.failed = 1;
        }
    }

    if (code != CURLE_OK || sink.failed) {
        if (request.outputPath) {
            unlink(request.outputPath.fileSystemRepresentation);
        }
        if (sink.overflow) {
            *error = KTKMakeError(KTKErrorTooLarge, @"Ответ слишком большой.");
        } else if (sink.failed) {
            *error = KTKMakeError(KTKErrorFile, @"Не удалось записать файл.");
        } else {
            NSString *message = errorBuffer[0] ? [NSString stringWithUTF8String:errorBuffer] : nil;
            *error = KTKMakeError(KTKErrorNetwork, message ?: [NSString stringWithUTF8String:curl_easy_strerror(code)]);
        }
        return nil;
    }

    KTKHTTPResponse *response = [KTKHTTPResponse new];
    response.status = status;
    response.body = request.outputPath ? nil : data;
    NSMutableArray *cookies = [NSMutableArray array];
    NSCharacterSet *space = [NSCharacterSet whitespaceAndNewlineCharacterSet];
    for (NSString *line in headerLines) {
        NSRange colon = [line rangeOfString:@":"];
        if (colon.location == NSNotFound) {
            continue;
        }
        NSString *name = [[line substringToIndex:colon.location] lowercaseString];
        NSString *value = [[line substringFromIndex:colon.location + 1] stringByTrimmingCharactersInSet:space];
        if ([name isEqualToString:@"set-cookie"]) {
            [cookies addObject:value];
        } else if ([name isEqualToString:@"location"]) {
            response.location = value;
        }
    }
    response.setCookies = cookies;
    return response;
}

@end
