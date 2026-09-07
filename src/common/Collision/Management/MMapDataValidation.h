/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 * Distributed under the GNU General Public License, version 2 or later.
 */

#ifndef TRINITY_MMAP_DATA_VALIDATION_H
#define TRINITY_MMAP_DATA_VALIDATION_H

#include "MMapDefines.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace MMAP
{
    // Detour's file reader trusts counts/indices and does not check dataSize.
    // Validate the serialized layout before it performs pointer arithmetic or
    // builds links. This also rejects a 32-bit-link tile in a 64-bit-link build,
    // even when its outer MMAP and Detour version numbers happen to match.
    inline char const* ValidateMMapTileData(unsigned char const* data, uint32 size)
    {
        if (size < sizeof(dtMeshHeader))
            return "truncated Detour header";
        dtMeshHeader header;
        std::memcpy(&header, data, sizeof(header));
        if (header.magic != DT_NAVMESH_MAGIC || header.version != DT_NAVMESH_VERSION)
            return "incompatible Detour magic/version";
        if (header.x == std::numeric_limits<int>::min() || header.x == std::numeric_limits<int>::max() ||
            header.y == std::numeric_limits<int>::min() || header.y == std::numeric_limits<int>::max() || header.layer < 0)
            return "invalid Detour tile coordinates";
        if (header.polyCount <= 0 || header.vertCount <= 0 || header.maxLinkCount <= 0 ||
            header.detailMeshCount < 0 || header.detailVertCount < 0 || header.detailTriCount < 0 ||
            header.bvNodeCount < 0 || header.offMeshConCount < 0 || header.offMeshConCount > header.polyCount ||
            header.offMeshBase != header.polyCount - header.offMeshConCount || header.detailMeshCount != header.offMeshBase)
            return "invalid Detour element counts";

        for (uint32 axis = 0; axis < 3; ++axis)
            if (!std::isfinite(header.bmin[axis]) || !std::isfinite(header.bmax[axis]) || header.bmin[axis] > header.bmax[axis])
                return "invalid Detour bounds";
        if (!std::isfinite(header.walkableHeight) || !std::isfinite(header.walkableRadius) ||
            !std::isfinite(header.walkableClimb) || !std::isfinite(header.bvQuantFactor) || header.bvQuantFactor <= 0.0f)
            return "invalid Detour dimensions";

        auto aligned = [](uint64 bytes) { return (bytes + 3) & ~uint64(3); };
        uint64 vertices = aligned(sizeof(header));
        uint64 polygons = vertices + aligned(uint64(header.vertCount) * 3 * sizeof(float));
        uint64 links = polygons + aligned(uint64(header.polyCount) * sizeof(dtPoly));
        uint64 details = links + aligned(uint64(header.maxLinkCount) * sizeof(dtLink));
        uint64 detailVertices = details + aligned(uint64(header.detailMeshCount) * sizeof(dtPolyDetail));
        uint64 triangles = detailVertices + aligned(uint64(header.detailVertCount) * 3 * sizeof(float));
        uint64 tree = triangles + aligned(uint64(header.detailTriCount) * 4);
        uint64 connections = tree + aligned(uint64(header.bvNodeCount) * sizeof(dtBVNode));
        uint64 expected = connections + aligned(uint64(header.offMeshConCount) * sizeof(dtOffMeshConnection));
        if (expected != size || expected > uint64(std::numeric_limits<int>::max()))
            return "Detour payload size/layout mismatch (including polygon-reference width)";

        auto verts = reinterpret_cast<float const*>(data + vertices);
        for (uint64 i = 0; i < uint64(header.vertCount) * 3; ++i)
            if (!std::isfinite(verts[i]))
                return "non-finite vertex";
        auto detailVerts = reinterpret_cast<float const*>(data + detailVertices);
        for (uint64 i = 0; i < uint64(header.detailVertCount) * 3; ++i)
            if (!std::isfinite(detailVerts[i]))
                return "non-finite detail vertex";

        auto polys = reinterpret_cast<dtPoly const*>(data + polygons);
        for (int i = 0; i < header.polyCount; ++i)
        {
            dtPoly const& poly = polys[i];
            bool offMesh = i >= header.offMeshBase;
            if (poly.vertCount > DT_VERTS_PER_POLYGON || (offMesh ? poly.vertCount != 2 : poly.vertCount < 3) ||
                poly.getType() != (offMesh ? DT_POLYTYPE_OFFMESH_CONNECTION : DT_POLYTYPE_GROUND))
                return "invalid polygon type/vertex count";
            for (uint32 j = 0; j < poly.vertCount; ++j)
                if (poly.verts[j] >= header.vertCount || (!(poly.neis[j] & DT_EXT_LINK) && poly.neis[j] > header.polyCount))
                    return "polygon index out of bounds";
        }
        auto detail = reinterpret_cast<dtPolyDetail const*>(data + details);
        auto tris = data + triangles;
        for (int i = 0; i < header.detailMeshCount; ++i)
        {
            if (uint64(detail[i].vertBase) + detail[i].vertCount > uint64(header.detailVertCount) ||
                uint64(detail[i].triBase) + detail[i].triCount > uint64(header.detailTriCount))
                return "detail mesh index out of bounds";
            for (uint32 t = 0; t < detail[i].triCount; ++t)
                for (uint32 v = 0; v < 3; ++v)
                    if (tris[(uint64(detail[i].triBase) + t) * 4 + v] >= polys[i].vertCount + detail[i].vertCount)
                        return "detail triangle index out of bounds";
        }
        auto nodes = reinterpret_cast<dtBVNode const*>(data + tree);
        for (int i = 0; i < header.bvNodeCount; ++i)
            if ((nodes[i].i >= 0 && nodes[i].i >= header.offMeshBase) ||
                (nodes[i].i < 0 && -int64(nodes[i].i) > header.bvNodeCount - i))
                return "bounding-volume index out of bounds";
        auto offMesh = reinterpret_cast<dtOffMeshConnection const*>(data + connections);
        for (int i = 0; i < header.offMeshConCount; ++i)
        {
            if (offMesh[i].poly < header.offMeshBase || offMesh[i].poly >= header.polyCount ||
                !std::isfinite(offMesh[i].rad) || offMesh[i].rad < 0.0f)
                return "invalid off-mesh connection";
            for (float value : offMesh[i].pos)
                if (!std::isfinite(value))
                    return "non-finite off-mesh connection";
        }
        return nullptr;
    }
}

#endif
