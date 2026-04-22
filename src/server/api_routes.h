#pragma once

#include "core/database.h"
#include "core/indexer.h"
#include "core/searcher.h"
#include "core/watcher.h"
#include "core/config.h"

namespace httplib { class Server; }

namespace fastsearch {

void register_api_routes(httplib::Server& svr,
                         Database& db,
                         Indexer& indexer,
                         Searcher& searcher,
                         Watcher& watcher,
                         Config& config);

} // namespace fastsearch
