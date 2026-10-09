/** Internal: configuration of the host-folder backed FatFs implementation. */
#pragma once
#include <string>
namespace chompi_sim
{
namespace dev
{
void SetCardRoot(const std::string& root);
void SetCardPresent(bool present);
} // namespace dev
} // namespace chompi_sim
