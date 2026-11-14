#include "AtemClient.h"

std::unique_ptr<AtemClient> createAtemClientMock();

#ifdef HAVE_ATEM_SDK
std::unique_ptr<AtemClient> createAtemClientSdk();
#endif

std::unique_ptr<AtemClient> createAtemClient()
{
#ifdef HAVE_ATEM_SDK
    return createAtemClientSdk();
#else
    return createAtemClientMock();
#endif
}