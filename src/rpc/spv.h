// Copyright (c) 2024 TeslaChain contributors
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPC_SPV_H
#define BITCOIN_RPC_SPV_H

#include <rpc/server.h>

namespace spv {

void RegisterSPVRPCCommands(CRPCTable& t);

} // namespace spv

#endif // BITCOIN_RPC_SPV_H