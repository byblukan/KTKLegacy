/* KTK for Symbian - the trust anchors for the site's certificate chain (generated, see
 * tools/gen-trust-anchors.py). */
#ifndef KTK_TRUSTANCHORS_H
#define KTK_TRUSTANCHORS_H

#include "bearssl.h"

#ifdef __cplusplus
extern "C" {
#endif

extern br_x509_trust_anchor KTK_TRUST_ANCHORS[];
extern const size_t KTK_TRUST_ANCHORS_NUM;

/* Fills KTK_TRUST_ANCHORS. Idempotent; call once on the main thread before any connection
 * (the Client constructor does). */
void ktk_trust_anchors_init(void);

#ifdef __cplusplus
}
#endif

#endif
