#pragma once

#define BEGIN_DECLARE_NAMESPACE(header,space) namespace header { namespace space {
#define END_DECLARE_NAMESPACE(header,space)   } }

#define BEGIN_CAM_NAMESPACE() BEGIN_DECLARE_NAMESPACE(rle4k,cam)
#define END_CAM_NAMESPACE()   END_DECLARE_NAMESPACE(rle4k,cam)

#define BEGIN_SP2_NAMESPACE() BEGIN_DECLARE_NAMESPACE(rle4k,fill)
#define END_SP2_NAMESPACE()   END_DECLARE_NAMESPACE(rle4k,fill)

#define USING_CAM_NAMESPACE() using namespace rle4k::cam
#define USING_SP2_NAMESPACE() using namespace rle4k::fill

#define NM_CAM rle4k::cam
#define NM_SP2 rle4k::fill
