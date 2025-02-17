//
// Copyright (c) 2009-2010 Mikko Mononen memon@inside.org
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//

#include "Recast/Include/Recast.h"
#include "NavEditor/Include/InputGeom.h"
#include "NavEditor/Include/ChunkyTriMesh.h"
#include "NavEditor/Include/MeshLoaderObj.h"
#include "NavEditor/Include/MeshLoaderPly.h"
#include "DebugUtils/Include/DebugDraw.h"
#include "DebugUtils/Include/RecastDebugDraw.h"
#include "Detour/Include/DetourNavMesh.h"
#include "NavEditor/Include/Editor.h"
#include <naveditor/include/GameUtils.h>

static inline bool intersectSegmentTriangle(const float* sp, const float* sq,
									 const float* a, const float* b, const float* c,
									 float &t, const bool delayedDiv)
{
	float v, w;
	float ab[3], ac[3], qp[3], ap[3], norm[3], e[3];
	rdVsub(ab, b, a);
	rdVsub(ac, c, a);
	rdVsub(qp, sp, sq);
	
	// Compute triangle normal. Can be precalculated or cached if
	// intersecting multiple segments against the same triangle
	rdVcross(norm, ab, ac);
	
	// Compute denominator d. If d <= 0, segment is parallel to or points
	// away from triangle, so exit early
	float d = rdVdot(qp, norm);
	if (d <= 0.0f) return false;
	
	// Compute intersection t value of pq with plane of triangle. A ray
	// intersects if 0 <= t. Segment intersects if 0 <= t <= 1. Delay
	// dividing by d until intersection has been found to pierce triangle
	rdVsub(ap, sp, a);
	t = rdVdot(ap, norm);
	if (t < 0.0f) return false;
	if (t > d) return false; // For segment; exclude this code line for a ray test
	
	// Compute barycentric coordinate components and test if within bounds
	rdVcross(e, qp, ap);
	v = rdVdot(ac, e);
	if (v < 0.0f || v > d) return false;
	w = -rdVdot(ab, e);
	if (w < 0.0f || v + w > d) return false;
	
	if (delayedDiv)
	{
		// Segment/ray intersects triangle. Perform delayed division
		t /= d;
	}
	
	return true;
}

static char* parseRow(char* buf, char* bufEnd, char* row, int len)
{
	bool start = true;
	bool done = false;
	int n = 0;
	while (!done && buf < bufEnd)
	{
		char c = *buf;
		buf++;
		// multirow
		switch (c)
		{
			case '\n':
				if (start) break;
				done = true;
				break;
			case '\r':
				break;
			case '\t':
			case ' ':
				if (start) break;
				// else falls through
			default:
				start = false;
				row[n++] = c;
				if (n >= len-1)
					done = true;
				break;
		}
	}
	row[n] = '\0';
	return buf;
}



InputGeom::InputGeom() :
	m_chunkyMesh(0),
	m_mesh(0),
	m_hasBuildSettings(false),
	m_offMeshConCount(0),
	m_volumeCount(0)
{
	rdVset(m_meshBMin, 0.0f, 0.0f, 0.0f);
	rdVset(m_meshBMax, 0.0f, 0.0f, 0.0f);
	rdVset(m_navMeshBMin, 0.0f, 0.0f, 0.0f);
	rdVset(m_navMeshBMax, 0.0f, 0.0f, 0.0f);
}

InputGeom::~InputGeom()
{
	delete m_chunkyMesh;
	delete m_mesh;
}
		
bool InputGeom::loadMesh(rcContext* ctx, const std::string& filepath)
{
	if (m_mesh)
	{
		delete m_chunkyMesh;
		m_chunkyMesh = 0;
		delete m_mesh;
		m_mesh = 0;
	}
	m_offMeshConCount = 0;
	m_volumeCount = 0;
	
	m_mesh = new rcMeshLoaderObj;
	if (!m_mesh)
	{
		ctx->log(RC_LOG_ERROR, "loadMesh: Out of memory 'm_mesh'.");
		return false;
	}
	if (!m_mesh->load(filepath))
	{
		ctx->log(RC_LOG_ERROR, "buildTiledNavigation: Could not load '%s'", filepath.c_str());
		return false;
	}

	rcCalcBounds(m_mesh->getVerts(), m_mesh->getVertCount(), m_meshBMin, m_meshBMax);
	rdVcopy(m_navMeshBMin, m_meshBMin);
	rdVcopy(m_navMeshBMax, m_meshBMax);

	m_chunkyMesh = new rcChunkyTriMesh;
	if (!m_chunkyMesh)
	{
		ctx->log(RC_LOG_ERROR, "buildTiledNavigation: Out of memory 'm_chunkyMesh'.");
		return false;
	}
	if (!rcCreateChunkyTriMesh(m_mesh->getVerts(), m_mesh->getTris(), m_mesh->getTriCount(), 256, m_chunkyMesh))
	{
		ctx->log(RC_LOG_ERROR, "buildTiledNavigation: Failed to build chunky mesh.");
		return false;
	}		

	return true;
}
bool InputGeom::loadPlyMesh(rcContext* ctx, const std::string& filepath)
{
	if (m_mesh)
	{
		delete m_chunkyMesh;
		m_chunkyMesh = 0;
		delete m_mesh;
		m_mesh = 0;
	}
	m_offMeshConCount = 0;
	m_volumeCount = 0;

	m_mesh = new rcMeshLoaderPly;
	if (!m_mesh)
	{
		ctx->log(RC_LOG_ERROR, "loadMesh: Out of memory 'm_mesh'.");
		return false;
	}
	if (!m_mesh->load(filepath))
	{
		ctx->log(RC_LOG_ERROR, "buildTiledNavigation: Could not load '%s'", filepath.c_str());
		return false;
	}

	rcCalcBounds(m_mesh->getVerts(), m_mesh->getVertCount(), m_meshBMin, m_meshBMax);
	rdVcopy(m_navMeshBMin, m_meshBMin);
	rdVcopy(m_navMeshBMax, m_meshBMax);

	m_chunkyMesh = new rcChunkyTriMesh;
	if (!m_chunkyMesh)
	{
		ctx->log(RC_LOG_ERROR, "buildTiledNavigation: Out of memory 'm_chunkyMesh'.");
		return false;
	}
	if (!rcCreateChunkyTriMesh(m_mesh->getVerts(), m_mesh->getTris(), m_mesh->getTriCount(), 256, m_chunkyMesh))
	{
		ctx->log(RC_LOG_ERROR, "buildTiledNavigation: Failed to build chunky mesh.");
		return false;
	}

	return true;
}
bool InputGeom::loadGeomSet(rcContext* ctx, const std::string& filepath)
{
	//NB(warmist): tf2 not implemented here
	char* buf = 0;
	FILE* fp = fopen(filepath.c_str(), "rb");
	if (!fp)
	{
		return false;
	}
	if (fseek(fp, 0, SEEK_END) != 0)
	{
		fclose(fp);
		return false;
	}

	long bufSize = ftell(fp);
	if (bufSize < 0)
	{
		fclose(fp);
		return false;
	}
	if (fseek(fp, 0, SEEK_SET) != 0)
	{
		fclose(fp);
		return false;
	}
	buf = new char[bufSize];
	if (!buf)
	{
		fclose(fp);
		return false;
	}
	size_t readLen = fread(buf, bufSize, 1, fp);
	fclose(fp);
	if (readLen != 1)
	{
		delete[] buf;
		return false;
	}
	
	m_offMeshConCount = 0;
	m_volumeCount = 0;
	delete m_mesh;
	m_mesh = 0;

	char* src = buf;
	char* srcEnd = buf + bufSize;
	char row[512];
	while (src < srcEnd)
	{
		// Parse one row
		row[0] = '\0';
		src = parseRow(src, srcEnd, row, sizeof(row)/sizeof(char));
		if (row[0] == 'f')
		{
			// File name.
			const char* name = row+1;
			// Skip white spaces
			while (*name && isspace(*name))
				name++;
			if (*name)
			{
				if (!loadMesh(ctx, name))
				{
					delete [] buf;
					return false;
				}
			}
		}
		else if (row[0] == 'c')
		{
			// Off-mesh connection
			if (m_offMeshConCount < MAX_OFFMESH_CONNECTIONS)
			{
				float* verts = &m_offMeshConVerts[m_offMeshConCount*3*2];
				float* refs = &m_offMeshConRefPos[m_offMeshConCount*3];
				float rad;
				float yaw;
				int bidir, area = 0, flags = 0;
				sscanf(row+1, "%f %f %f %f %f %f %f %d %d %d %f %f %f %f",
					   &verts[0], &verts[1], &verts[2],
					   &verts[3], &verts[4], &verts[5],
					   &rad,
					   &bidir, &area, &flags,
					   &refs[0], &refs[1], &refs[2],
					   &yaw);
				m_offMeshConRads[m_offMeshConCount] = rad;
				m_offMeshConDirs[m_offMeshConCount] = (unsigned char)bidir;
				m_offMeshConAreas[m_offMeshConCount] = (unsigned char)area;
				m_offMeshConFlags[m_offMeshConCount] = (unsigned short)flags;
				m_offMeshConRefYaws[m_offMeshConCount] = yaw;
				m_offMeshConCount++;
			}
		}
		else if (row[0] == 'v')
		{
			// Convex volumes
			if (m_volumeCount < MAX_VOLUMES)
			{
				ConvexVolume* vol = &m_volumes[m_volumeCount++];
				sscanf(row+1, "%d %d %f %f", &vol->nverts, &vol->area, &vol->hmin, &vol->hmax);
				for (int i = 0; i < vol->nverts; ++i)
				{
					row[0] = '\0';
					src = parseRow(src, srcEnd, row, sizeof(row)/sizeof(char));
					sscanf(row, "%f %f %f", &vol->verts[i*3+0], &vol->verts[i*3+1], &vol->verts[i*3+2]);
				}
			}
		}
		else if (row[0] == 's')
		{
			// Settings
			m_hasBuildSettings = true;
			sscanf(row + 1, "%f %f %f %f %f %f %d %d %d %f %d %d %f %f %d %f %f %f %f %f %f %d",
							&m_buildSettings.cellSize,
							&m_buildSettings.cellHeight,
							&m_buildSettings.agentHeight,
							&m_buildSettings.agentRadius,
							&m_buildSettings.agentMaxClimb,
							&m_buildSettings.agentMaxSlope,
							&m_buildSettings.regionMinSize,
							&m_buildSettings.regionMergeSize,
							&m_buildSettings.edgeMaxLen,
							&m_buildSettings.edgeMaxError,
							&m_buildSettings.vertsPerPoly,
							&m_buildSettings.polyCellRes,
							&m_buildSettings.detailSampleDist,
							&m_buildSettings.detailSampleMaxError,
							&m_buildSettings.partitionType,
							&m_buildSettings.navMeshBMin[0],
							&m_buildSettings.navMeshBMin[1],
							&m_buildSettings.navMeshBMin[2],
							&m_buildSettings.navMeshBMax[0],
							&m_buildSettings.navMeshBMax[1],
							&m_buildSettings.navMeshBMax[2],
							&m_buildSettings.tileSize);

			// Copy the original values over so we can reset to them in the
			// editor after changes have been made.
			rdVcopy(m_buildSettings.origNavMeshBMin, m_buildSettings.navMeshBMin);
			rdVcopy(m_buildSettings.origNavMeshBMax, m_buildSettings.navMeshBMax);
		}
	}
	
	delete [] buf;
	
	return true;
}

bool InputGeom::load(rcContext* ctx, const std::string& filepath)
{
	size_t extensionPos = filepath.find_last_of('.');
	if (extensionPos == std::string::npos)
		return false;

	std::string extension = filepath.substr(extensionPos);
	std::transform(extension.begin(), extension.end(), extension.begin(),
		[](unsigned char c) { return char(::tolower(c)); });

	if (extension == ".gset")
		return loadGeomSet(ctx, filepath);
	if (extension == ".obj")
		return loadMesh(ctx, filepath);
	if (extension == ".ply")
		return loadPlyMesh(ctx, filepath);

	return false;
}

bool InputGeom::saveGeomSet(const BuildSettings* settings)
{
	if (!m_mesh) return false;
	
	// Change extension
	std::string filepath = m_mesh->getFileName();
	size_t extPos = filepath.find_last_of('.');
	if (extPos != std::string::npos)
		filepath = filepath.substr(0, extPos);

	filepath += ".gset";

	FILE* fp = fopen(filepath.c_str(), "w");
	if (!fp) return false;
	
	// Store mesh filename.
	fprintf(fp, "f %s\n", m_mesh->getFileName().c_str());

	// Store settings if any
	if (settings)
	{
		fprintf(fp,
			"s %f %f %f %f %f %f %d %d %d %f %d %d %f %f %d %f %f %f %f %f %f %d\n",
			settings->cellSize,
			settings->cellHeight,
			settings->agentHeight,
			settings->agentRadius,
			settings->agentMaxClimb,
			settings->agentMaxSlope,
			settings->regionMinSize,
			settings->regionMergeSize,
			settings->edgeMaxLen,
			settings->edgeMaxError,
			settings->vertsPerPoly,
			settings->polyCellRes,
			settings->detailSampleDist,
			settings->detailSampleMaxError,
			settings->partitionType,
			settings->navMeshBMin[0],
			settings->navMeshBMin[1],
			settings->navMeshBMin[2],
			settings->navMeshBMax[0],
			settings->navMeshBMax[1],
			settings->navMeshBMax[2],
			settings->tileSize);
	}
	
	// Store off-mesh links.
	for (int i = 0; i < m_offMeshConCount; ++i)
	{
		const float* verts = &m_offMeshConVerts[i*3*2];
		const float* refs = &m_offMeshConRefPos[i*3];
		const float rad = m_offMeshConRads[i];
		const float yaw = m_offMeshConRefYaws[i];
		const int bidir = m_offMeshConDirs[i];
		const int area = m_offMeshConAreas[i];
		const int flags = m_offMeshConFlags[i];
		fprintf(fp, "c %f %f %f %f %f %f %f %d %d %d %f %f %f %f\n",
				verts[0], verts[1], verts[2],
				verts[3], verts[4], verts[5],
				rad,
				bidir, area, flags,
				refs[0], refs[1], refs[2],
				yaw);
	}

	// Convex volumes
	for (int i = 0; i < m_volumeCount; ++i)
	{
		ConvexVolume* vol = &m_volumes[i];
		fprintf(fp, "v %d %d %f %f\n", vol->nverts, vol->area, vol->hmin, vol->hmax);
		for (int j = 0; j < vol->nverts; ++j)
			fprintf(fp, "%f %f %f\n", vol->verts[j*3+0], vol->verts[j*3+1], vol->verts[j*3+2]);
	}
	
	fclose(fp);
	
	return true;
}

static bool isectSegAABB(const float* sp, const float* sq,
						 const float* amin, const float* amax,
						 float& tmin, float& tmax)
{
	float d[3];
	d[0] = sq[0] - sp[0];
	d[1] = sq[1] - sp[1];
	d[2] = sq[2] - sp[2];
	tmin = 0.0;
	tmax = 1.0f;
	
	for (int i = 0; i < 3; i++)
	{
		if (rdMathFabsf(d[i]) < RD_EPS)
		{
			if (sp[i] < amin[i] || sp[i] > amax[i])
				return false;
		}
		else
		{
			const float ood = 1.0f / d[i];
			float t1 = (amin[i] - sp[i]) * ood;
			float t2 = (amax[i] - sp[i]) * ood;
			if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
			if (t1 > tmin) tmin = t1;
			if (t2 < tmax) tmax = t2;
			if (tmin > tmax) return false;
		}
	}
	
	return true;
}


bool InputGeom::raycastMesh(const float* src, const float* dst, float* tmin) const
{
	// Prune hit ray.
	float btmin, btmax;
	if (!isectSegAABB(src, dst, m_meshBMin, m_meshBMax, btmin, btmax))
		return false;
	float p[2], q[2];
	p[0] = src[0] + (dst[0]-src[0])*btmin;
	p[1] = src[1] + (dst[1]-src[1])*btmin;
	q[0] = src[0] + (dst[0]-src[0])*btmax;
	q[1] = src[1] + (dst[1]-src[1])*btmax;
	
	int cid[4096]; //stack is crying for help
	const int ncid = rcGetChunksOverlappingSegment(m_chunkyMesh, p, q, cid, 4096);
	if (!ncid)
		return false;
	
	float localtmin = 1.0f;
	const bool calcTmin = tmin != nullptr;

	bool hit = false;
	const float* verts = m_mesh->getVerts();
	
	for (int i = 0; i < ncid; ++i)
	{
		const rcChunkyTriMeshNode& node = m_chunkyMesh->nodes[cid[i]];
		const int* tris = &m_chunkyMesh->tris[node.i*3];
		const int ntris = node.n;

		for (int j = 0; j < ntris*3; j += 3)
		{
			float t = 1;
			if (intersectSegmentTriangle(src, dst,
										 &verts[tris[j]*3],
										 &verts[tris[j+1]*3],
										 &verts[tris[j+2]*3], t, calcTmin))
			{
				hit = true;

				if (calcTmin && t < localtmin)
					localtmin = t;
				else
					break;
			}
		}
	}

	if (calcTmin)
		*tmin = localtmin;
	
	return hit;
}

void InputGeom::addOffMeshConnection(const float* spos, const float* epos, const float rad,
									 unsigned char bidir, unsigned char jump, unsigned char order,
									 unsigned char area, unsigned short flags)
{
	if (m_offMeshConCount >= MAX_OFFMESH_CONNECTIONS) return;
	rdAssert(jump < DT_MAX_TRAVERSE_TYPES);

	float* refs = &m_offMeshConRefPos[m_offMeshConCount*3];
	float* verts = &m_offMeshConVerts[m_offMeshConCount*3*2];
	float yaw = dtCalcOffMeshRefYaw(spos, epos);

	dtCalcOffMeshRefPos(spos, yaw, DT_OFFMESH_CON_REFPOS_OFFSET, refs);

	m_offMeshConRads[m_offMeshConCount] = rad;
	m_offMeshConRefYaws[m_offMeshConCount] = yaw;
	m_offMeshConDirs[m_offMeshConCount] = bidir;
	m_offMeshConJumps[m_offMeshConCount] = jump;
	m_offMeshConOrders[m_offMeshConCount] = order;
	m_offMeshConAreas[m_offMeshConCount] = area;
	m_offMeshConFlags[m_offMeshConCount] = flags;
	m_offMeshConId[m_offMeshConCount] = 1000 + m_offMeshConCount;
	rdVcopy(&verts[0], spos);
	rdVcopy(&verts[3], epos);
	m_offMeshConCount++;
}

void InputGeom::deleteOffMeshConnection(int i)
{
	m_offMeshConCount--;
	float* vertsSrc = &m_offMeshConVerts[m_offMeshConCount*3*2];
	float* vertsDst = &m_offMeshConVerts[i*3*2];
	float* refSrc = &m_offMeshConRefPos[m_offMeshConCount*3];
	float* refDst = &m_offMeshConRefPos[i*3];
	rdVcopy(&vertsDst[0], &vertsSrc[0]);
	rdVcopy(&vertsDst[3], &vertsSrc[3]);
	rdVcopy(&refDst[0], &refSrc[0]);
	m_offMeshConRads[i] = m_offMeshConRads[m_offMeshConCount];
	m_offMeshConRefYaws[i] = m_offMeshConRefYaws[m_offMeshConCount];
	m_offMeshConDirs[i] = m_offMeshConDirs[m_offMeshConCount];
	m_offMeshConJumps[i] = m_offMeshConJumps[m_offMeshConCount];
	m_offMeshConOrders[i] = m_offMeshConOrders[m_offMeshConCount];
	m_offMeshConAreas[i] = m_offMeshConAreas[m_offMeshConCount];
	m_offMeshConFlags[i] = m_offMeshConFlags[m_offMeshConCount];
}

void InputGeom::drawOffMeshConnections(duDebugDraw* dd, const float* offset, bool hilight)
{
	unsigned int conColor = duRGBA(192,0,128,192);
	unsigned int baseColor = duRGBA(0,0,0,64);
	dd->depthMask(false);

	dd->begin(DU_DRAW_LINES, 2.0f, offset);
	for (int i = 0; i < m_offMeshConCount; ++i)
	{
		float* v = &m_offMeshConVerts[i*3*2];

		dd->vertex(v[0],v[1],v[2], baseColor);
		dd->vertex(v[0],v[1],v[2]+10.0f, baseColor);
		
		dd->vertex(v[3],v[4],v[5], baseColor);
		dd->vertex(v[3],v[4],v[5]+10.0f, baseColor);
		
		duAppendCircle(dd, v[0],v[1],v[2]+5.0f, m_offMeshConRads[i], baseColor);
		duAppendCircle(dd, v[3],v[4],v[5]+5.0f, m_offMeshConRads[i], baseColor);

		if (hilight)
		{
			duAppendArc(dd, v[0],v[1],v[2], v[3],v[4],v[5], 0.25f,
						(m_offMeshConDirs[i]&DT_OFFMESH_CON_BIDIR) ? 30.0f : 0.0f, 30.0f, conColor);
		}

		float* r = &m_offMeshConRefPos[i*3];
		float refPosDir[3];

		dtCalcOffMeshRefPos(r, m_offMeshConRefYaws[i], DT_OFFMESH_CON_REFPOS_OFFSET, refPosDir);

		duAppendArrow(dd, r[0],r[1],r[2], refPosDir[0],refPosDir[1],refPosDir[2], 0.f, 10.f, duRGBA(255,255,0,255));
	}	
	dd->end();

	dd->depthMask(true);
}

void InputGeom::addConvexVolume(const float* verts, const int nverts,
								const float minh, const float maxh, unsigned char area)
{
	if (m_volumeCount >= MAX_VOLUMES) return;
	ConvexVolume* vol = &m_volumes[m_volumeCount++];
	memset(vol, 0, sizeof(ConvexVolume));
	memcpy(vol->verts, verts, sizeof(float)*3*nverts);
	vol->hmin = minh;
	vol->hmax = maxh;
	vol->nverts = nverts;
	vol->area = area;
}

void InputGeom::deleteConvexVolume(int i)
{
	m_volumeCount--;
	m_volumes[i] = m_volumes[m_volumeCount];
}

void InputGeom::drawConvexVolumes(struct duDebugDraw* dd, const float* offset, bool /*hilight*/)
{
	dd->depthMask(false);

	dd->begin(DU_DRAW_TRIS, 1.0f, offset);
	
	for (int i = 0; i < m_volumeCount; ++i)
	{
		const ConvexVolume* vol = &m_volumes[i];
		unsigned int col;

		if (vol->area == RC_NULL_AREA)
			col = duRGBA(255, 0, 0, 128); // Use red for visibility (null acts as deletion).
		else
			col = duTransCol(dd->areaToCol(vol->area), 64);

		for (int j = 0, k = vol->nverts-1; j < vol->nverts; k = j++)
		{
			const float* va = &vol->verts[k*3];
			const float* vb = &vol->verts[j*3];

			dd->vertex(va[0],va[1],vol->hmax, col);
			dd->vertex(vb[0],vb[1],vol->hmax, col);
			dd->vertex(vol->verts[0],vol->verts[1],vol->hmax, col);

			dd->vertex(vb[0],vb[1],vol->hmax, col);
			dd->vertex(va[0],va[1],vol->hmax, col);
			dd->vertex(va[0],va[1],vol->hmin, duDarkenCol(col));

			dd->vertex(vb[0],vb[1],vol->hmin, duDarkenCol(col));
			dd->vertex(vb[0],vb[1],vol->hmax, col);
			dd->vertex(va[0],va[1],vol->hmin, duDarkenCol(col));
		}
	}
	
	dd->end();

	dd->begin(DU_DRAW_LINES, 2.0f, offset);
	for (int i = 0; i < m_volumeCount; ++i)
	{
		const ConvexVolume* vol = &m_volumes[i];
		unsigned int col;

		if (vol->area == RC_NULL_AREA)
			col = duRGBA(255, 0, 0, 220);
		else
			col = duTransCol(dd->areaToCol(vol->area), 220);

		for (int j = 0, k = vol->nverts-1; j < vol->nverts; k = j++)
		{
			const float* va = &vol->verts[k*3];
			const float* vb = &vol->verts[j*3];
			dd->vertex(vb[0],vb[1],vol->hmin, duDarkenCol(col));
			dd->vertex(va[0],va[1],vol->hmin, duDarkenCol(col));
			dd->vertex(vb[0],vb[1],vol->hmax, col);
			dd->vertex(va[0],va[1],vol->hmax, col);
			dd->vertex(va[0],va[1],vol->hmax, col);
			dd->vertex(va[0],va[1],vol->hmin, duDarkenCol(col));
		}
	}
	dd->end();

	dd->begin(DU_DRAW_POINTS, 3.0f, offset);
	for (int i = 0; i < m_volumeCount; ++i)
	{
		const ConvexVolume* vol = &m_volumes[i];
		unsigned int col;

		if (vol->area == RC_NULL_AREA)
			col = duRGBA(255, 0, 0, 220);
		else
			col = duDarkenCol(duTransCol(dd->areaToCol(vol->area), 220));

		for (int j = 0; j < vol->nverts; ++j)
		{
			dd->vertex(vol->verts[j*3+0],vol->verts[j*3+1],vol->hmax, col);
			dd->vertex(vol->verts[j*3+0],vol->verts[j*3+1],vol->hmin, col);
			dd->vertex(vol->verts[j*3+0],vol->verts[j*3+1]+0.1f,vol->verts[j*3+2], col);
		}
	}
	dd->end();

	dd->depthMask(true);
}
