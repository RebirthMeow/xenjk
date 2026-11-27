"""w3tmesh.py -- Witcher 3 terrain tiles as render meshes without cracks, with the game's holes cut finely (w3scene.py).

The game draws a tile's holes per pixel: its shader's alpha is the bilinear share of the four control taps around a
point that are not holes (control word 0, w3build.solid_share), and the meshes that fill a hole (a wall's foot, a
cave mouth, a stair) cover the rest. Cutting whole grid triangles (0.98 m) by their centre's share left open sky by
a wall's foot (a hole line 1-2 taps wide, 0.5-1 m) and grass through a walkway, so the cells whose taps hold a hole
are split K x K and their pieces cut by their own centres (0.24 m on a 0.98 m grid, the share's 0.5 line to an
eighth of a metre).

Neighbouring cells keep every vertex of a shared edge: a cell beside a split one, and a far-ring cell beside the
full-resolution near tiles (every --far-step vertex against every one), is a fan from its centre over its edges'
vertices. The far ring's thinned edges against the near tiles' full ones had opened cracks along the near box (a
height miss up to 2.6 m between a far edge's straight line and the near edge it spans).

Points carry a key (tile, row and column in 1/K cells) so a chunk welds them; an edge point is the same arithmetic
from either side (from the edge's lower end), so tiles and chunks that share an edge share its points exactly."""
import numpy as np

K = 4


def hole_cells(lattice, n):
    """(n-1) x (n-1) cells of an n x n height grid over the same tile whose control taps include a hole (any tap
    the bilinear share reads inside the cell). lattice: (m x m) control words, (m - 1) a multiple of (n - 1)."""
    m = lattice.shape[0]
    r = (m - 1) // (n - 1)
    assert r * (n - 1) == m - 1, (m, n)
    H = lattice == 0
    out = np.zeros((n - 1, n - 1), bool)
    for di in range(r + 1):
        for dj in range(r + 1):
            out |= H[di:di + r * (n - 1):r, dj:dj + r * (n - 1):r]
    return out


class Tile:
    """A tile's thinned grid G (rows +y, columns +x, xyz), its normals GN, its split cells `sub` and the edges that
    carry split points: hs[i, j] the row-i edge from column j to j + 1, vs[i, j] the column-j edge from row i to
    i + 1. over: {side: polyline function} for a far tile's edges against a near tile ('b', 't', 'l', 'r')."""

    def __init__(self, key, G, GN, sub=None):
        self.key, self.G, self.GN = key, G, GN
        n, m = G.shape[:2]
        self.sub = sub if sub is not None else np.zeros((n - 1, m - 1), bool)
        self.hs = np.zeros((n, m - 1), bool)
        self.vs = np.zeros((n - 1, m), bool)
        self.hs[:-1] |= self.sub
        self.hs[1:] |= self.sub
        self.vs[:, :-1] |= self.sub
        self.vs[:, 1:] |= self.sub
        self.over = {}

    def point(self, FI, FJ):
        """(key, xyz, normal) at fine row FI, column FJ (1/K cells)"""
        n, m = self.G.shape[:2]
        i, di = divmod(FI, K)
        j, dj = divmod(FJ, K)
        if i == n - 1:
            i, di = n - 2, K
        if j == m - 1:
            j, dj = m - 2, K
        G = self.G
        v, u = di / K, dj / K
        if di in (0, K) and dj in (0, K):
            p = G[i + di // K, j + dj // K]
        elif di in (0, K):              # on a row edge: from its lower column
            a, b = G[i + di // K, j], G[i + di // K, j + 1]
            p = a + u * (b - a)
        elif dj in (0, K):              # on a column edge: from its lower row
            a, b = G[i, j + dj // K], G[i + 1, j + dj // K]
            p = a + v * (b - a)
        elif u >= v:                    # inside: the cell's two triangles, split from (i, j) to (i + 1, j + 1)
            p = G[i, j] + u * (G[i, j + 1] - G[i, j]) + v * (G[i + 1, j + 1] - G[i, j + 1])
        else:
            p = G[i, j] + v * (G[i + 1, j] - G[i, j]) + u * (G[i + 1, j + 1] - G[i + 1, j])
        N = self.GN
        nr = ((1 - u) * (1 - v) * N[i, j] + u * (1 - v) * N[i, j + 1] + (1 - u) * v * N[i + 1, j] +
              u * v * N[i + 1, j + 1])
        nr = nr / max(float(np.linalg.norm(nr)), 1e-12)
        return (self.key, FI, FJ), p, nr

    def edge_h(self, i, j):
        """the points of the row-i edge from column j to j + 1, in order"""
        if i == 0 and 'b' in self.over:
            return self.over['b'](j)
        if i == self.G.shape[0] - 1 and 't' in self.over:
            return self.over['t'](j)
        ks = range(K + 1) if self.hs[i, j] else (0, K)
        return [self.point(K * i, K * j + k) for k in ks]

    def edge_v(self, i, j):
        """the points of the column-j edge from row i to i + 1, in order"""
        if j == 0 and 'l' in self.over:
            return self.over['l'](i)
        if j == self.G.shape[1] - 1 and 'r' in self.over:
            return self.over['r'](i)
        ks = range(K + 1) if self.vs[i, j] else (0, K)
        return [self.point(K * i + k, K * j) for k in ks]

    def special(self):
        """cells that are not two plain triangles"""
        s = self.sub | self.hs[:-1] | self.hs[1:] | self.vs[:, :-1] | self.vs[:, 1:]
        n, m = self.G.shape[:2]
        if 'b' in self.over:
            s[0, :] = True
        if 't' in self.over:
            s[n - 2, :] = True
        if 'l' in self.over:
            s[:, 0] = True
        if 'r' in self.over:
            s[:, m - 2] = True
        return s


def link(near, far, size, rs):
    """Split points across tile borders. near, far: {(min x, min y) rounded: Tile}; size: the tiles' size; rs: far
    cells per near cell along an edge (--far-step / --terrain-step). A near edge splits where the cell on either side
    is split, its neighbour tile's too; a far tile's edge against a near tile takes that near edge's points."""
    for (x, y), T in near.items():
        for dx, dy, mine, theirs in ((size, 0, ('v', -1), ('l', 0)), (-size, 0, ('v', 0), ('r', -1)),
                                     (0, size, ('h', -1), ('b', 0)), (0, -size, ('h', 0), ('t', -1))):
            O = near.get((x + dx, y + dy))
            if O is None:
                continue
            if mine[0] == 'v':          # their first or last column of cells
                T.vs[:, mine[1]] |= O.sub[:, theirs[1]]
            else:
                T.hs[mine[1], :] |= O.sub[theirs[1], :]
    for (x, y), F in far.items():
        for dx, dy, mine, theirs in ((size, size, (-1, -1), (0, 0)), (-size, size, (-1, 0), (0, -1)),
                                     (size, -size, (0, -1), (-1, 0)), (-size, -size, (0, 0), (-1, -1))):
            T = near.get((x + dx, y + dy))    # a near tile across a corner only
            if T is not None:
                F.GN[mine] = T.GN[theirs]
        for dx, dy, side in ((0, -size, 'b'), (0, size, 't'), (-size, 0, 'l'), (size, 0, 'r')):
            T = near.get((x + dx, y + dy))
            if T is None:
                continue
            nn = T.G.shape[0]
            # the shared line's normals: the near tile's (the far grid's coarse differences shaded the edge as a line)
            if side == 'b':
                F.GN[0, :] = T.GN[nn - 1, ::rs]
            elif side == 't':
                F.GN[-1, :] = T.GN[0, ::rs]
            elif side == 'l':
                F.GN[:, 0] = T.GN[::rs, nn - 1]
            else:
                F.GN[:, -1] = T.GN[::rs, 0]

            def poly(c, T=T, side=side, nn=nn):
                pts = []
                for k in range(rs * c, rs * (c + 1)):
                    if side == 'b':         # the near tile below: its top row
                        e = T.edge_h(nn - 1, k)
                    elif side == 't':
                        e = T.edge_h(0, k)
                    elif side == 'l':       # the near tile to the left: its last column
                        e = T.edge_v(k, nn - 1)
                    else:
                        e = T.edge_v(k, 0)
                    pts += e if not pts else e[1:]
                return pts
            F.over[side] = poly


def chunk(T, i0, i1, j0, j1):
    """Cells [i0, i1) x [j0, j1) of tile T: (xyz, normals, triangles, fine) where the first (i1 - i0 + 1) x
    (j1 - j0 + 1) vertices are the grid's and `fine` marks the triangles of split cells (for the hole test)."""
    S = T.G[i0:i1 + 1, j0:j1 + 1]
    sn, sm = S.shape[:2]
    V = [S.reshape(-1, 3)]
    N = [T.GN[i0:i1 + 1, j0:j1 + 1].reshape(-1, 3)]
    extra_p, extra_n, index = [], [], {}
    base = sn * sm

    def vid(pt):
        key, p, nr = pt
        if key[0] == T.key and key[1] % K == 0 and key[2] % K == 0:
            i, j = key[1] // K - i0, key[2] // K - j0
            if 0 <= i < sn and 0 <= j < sm:
                return i * sm + j
        k = index.get(key)
        if k is None:
            k = index[key] = base + len(extra_p)
            extra_p.append(p)
            extra_n.append(nr)
        return k

    spec = T.special()[i0:i1, j0:j1]
    ii, jj = np.nonzero(~spec)
    q = ii * sm + jj
    tris = [np.stack([q, q + 1, q + sm + 1], 1), np.stack([q, q + sm + 1, q + sm], 1)]
    fine = [np.zeros(2 * len(q), bool)]
    for a, b in zip(*np.nonzero(spec)):
        i, j = int(a) + i0, int(b) + j0
        if T.sub[i, j]:                 # K x K pieces, each split like the grid
            ix = [[vid(T.point(K * i + r, K * j + c)) for c in range(K + 1)] for r in range(K + 1)]
            t = []
            for r in range(K):
                for c in range(K):
                    t.append((ix[r][c], ix[r][c + 1], ix[r + 1][c + 1]))
                    t.append((ix[r][c], ix[r + 1][c + 1], ix[r + 1][c]))
            tris.append(np.array(t, np.int64))
            fine.append(np.ones(len(t), bool))
            continue
        # a fan from the cell's centre (on its split line) over its edges' points, counter-clockwise from (i, j):
        # bottom (+x), right (+y), top (-x), left (-y)
        ring = (T.edge_h(i, j)[:-1] + T.edge_v(i, j + 1)[:-1] + T.edge_h(i + 1, j)[::-1][:-1] +
                T.edge_v(i, j)[::-1][:-1])
        ring = [vid(p) for p in ring]
        c = vid(((T.key + '#centre', i, j), (T.G[i, j] + T.G[i + 1, j + 1]) / 2,
                 (T.GN[i, j] + T.GN[i + 1, j + 1]) / max(float(np.linalg.norm(T.GN[i, j] + T.GN[i + 1, j + 1])), 1e-12)))
        t = [(c, ring[k], ring[(k + 1) % len(ring)]) for k in range(len(ring))]
        tris.append(np.array(t, np.int64))
        fine.append(np.zeros(len(t), bool))
    if extra_p:
        V.append(np.array(extra_p))
        N.append(np.array(extra_n))
    tri = np.concatenate([t.reshape(-1, 3) for t in tris]).astype(np.int64)
    return np.concatenate(V), np.concatenate(N), tri, np.concatenate(fine)
