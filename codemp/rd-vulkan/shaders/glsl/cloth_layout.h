/*
===========================================================================
Copyright (C) 2026, xenjk contributors

This file is part of xenjk, an OpenJK derivative.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/
// Garment cloth on the GPU: buffer layouts shared by vk_cloth_gpu.cpp and cloth_step.comp / cloth_draw.comp.
// Only #define lines: this file is valid C and GLSL. All offsets and sizes are in 4-byte words.

// ---- instance buffer: header (uint words), then the arrays at the word offsets it names
#define CLH_N            0    // particles
#define CLH_NT           1    // cloth triangles
#define CLH_M            2    // collider vertices (0 = no collider)
#define CLH_NDYN         3    // dynamic particles
#define CLH_NCAPS        4    // capsules
#define CLH_NEC          5    // edge colours
#define CLH_NBC          6    // bend colours
#define CLH_NAC          7    // area colours
#define CLH_R            8    // render vertices
#define CLH_RD           9    // dynamic render vertices
#define CLH_CMAXEDGE     10   // float bits: longest collider edge
#define CLH_O_X0         11   // N*3 f: rest positions
#define CLH_O_SKS        12   // N+1 u: skin CSR start
#define CLH_O_SKB        13   // u: skin bone
#define CLH_O_SKW        14   // f: skin weight
#define CLH_O_CP0        15   // M*3 f: collider rest positions
#define CLH_O_CSKS       16   // M+1 u
#define CLH_O_CSKB       17
#define CLH_O_CSKW       18
#define CLH_O_CTRI       19   // cNT*3 u: collider triangles
#define CLH_O_VTS        20   // M+1 u: collider vertex -> triangle CSR
#define CLH_O_VTL        21
#define CLH_O_VVS        22   // M+1 u: collider vertex -> vertex CSR (one-ring)
#define CLH_O_VVL        23
#define CLH_O_VRAD       24   // M f
#define CLH_O_W          25   // N f: inverse mass (0 = pinned)
#define CLH_O_KIN        26   // N u: pinned
#define CLH_O_ADMAP      27   // N f
#define CLH_O_MAXD       28   // N f
#define CLH_O_DYN        29   // NDYN u
#define CLH_O_EC         30   // NEC+1 u: first edge of each colour
#define CLH_O_E          31   // 3 words per edge: a, b (u), rest length (f)
#define CLH_O_BC         32   // NBC+1 u
#define CLH_O_B          33   // 6 words per hinge: i1..i4 (u), rest angle, rest wing distance (f)
#define CLH_O_AC         34   // NAC+1 u
#define CLH_O_A          35   // 4 words per triangle: a, b, c (u), rest area (f)
#define CLH_O_TETS       36   // N+1 u: tether CSR
#define CLH_O_TETA       37   // u: anchor
#define CLH_O_TETL       38   // f: rest length
#define CLH_O_TRI        39   // NT*3 u: cloth triangles
#define CLH_O_PTS        40   // N+1 u: particle -> cloth triangle CSR
#define CLH_O_PTL        41
#define CLH_O_CAPR       42   // NCAPS f
#define CLH_O_X          43   // N*3 f: state
#define CLH_O_V          44
#define CLH_O_P          45
#define CLH_O_TOLD       46
#define CLH_O_TNEW       47
#define CLH_O_TS         48
#define CLH_O_COLD       49   // M*3 f
#define CLH_O_CNEW       50
#define CLH_O_CS         51
#define CLH_O_CTRIX      52   // N i: contact triangle (-1 none)
#define CLH_O_CVERT      53   // N i: tracked collider vertex (-1 none)
#define CLH_O_PUSH       54   // N f
#define CLH_O_CNRM       55   // N*3 f
#define CLH_O_HHEAD      56   // CL_HASH i
#define CLH_O_HNEXT      57   // M i
#define CLH_O_TRIF       58   // NT*3 f: aero force per triangle
#define CLH_O_REL0       59   // N*3 f: step output in its pelvis frame (REL0..REL2 rotate: lead 2)
#define CLH_O_REL1       60
#define CLH_O_DPOS       61   // N*3 f: draw scratch (bind space)
#define CLH_O_DNRM       62
#define CLH_O_RSIM       63   // R*CL_RW_WORDS: render vertex wrap: particles i0..i2 (u), weights w0..w2, offset, sign (f)
#define CLH_O_RDYN       64   // RD u: dynamic render vertices
#define CLH_O_RTAN       65   // R*4 f: bind tangents of the render vertices
#define CLH_O_STRI       66   // N i: sidedContacts: face the particle was last resolved outside of (-1 none)
#define CLH_O_CONEC      67   // N f: coneBody: cosine of the allowed flare angle
#define CLH_O_REL2       68   // N*3 f: the third step state (r_clothDrawLead 2 blends the last three)
#define CLH_WORDS        69   // header size

#define CL_HASH          16384   // contact hash buckets (skirt_sim.c: 65536; the same vertices are found)
#define CL_MAX_CAPS      16
#define CL_RV_WORDS      11      // render vertex (the packed Ghoul2 vertex): pos 3 f, normal 2 (snorm16 x4), st 2 f, bones 1, weights 1, tangent 2 (snorm16 x4)
#define CL_RW_WORDS      8       // render vertex wrap record (CLH_O_RSIM)

// ---- step record (in the frame buffer, at the push constant's word offset)
#define CLR_RESET        0    // u
#define CLR_NSUB         1    // u
#define CLR_NIT          2    // u
#define CLR_RELOUT       3    // u: write REL0, REL1 or REL2 (0..2)
#define CLR_STATS        4    // u: word offset of this step's stats in the frame buffer
#define CLR_DT           5
#define CLR_H            6
#define CLR_F            7    // 12
#define CLR_D            19   // 9
#define CLR_DVEC         28   // 3
#define CLR_CARRY        31   // u
#define CLR_CARRYV       32   // 3
#define CLR_KICKIMP      35   // u
#define CLR_KICKV        36   // 3
#define CLR_KICKO        39   // 3
#define CLR_SPREAD       42
#define CLR_GL           43   // 3
#define CLR_WF           46   // 3
#define CLR_AEROLIN      49   // 3
#define CLR_AEROANG      52
#define CLR_VL           53   // 3
#define CLR_OL           56   // 3
#define CLR_KEDGE        59
#define CLR_KBEND        60
#define CLR_KAREA        61
#define CLR_KTETHER      62
#define CLR_KAD          63
#define CLR_DAD          64
#define CLR_DAMPF        65
#define CLR_ADUSEMAP     66   // u
#define CLR_CONECOS      67
#define CLR_CONEAXIS     68   // 3
#define CLR_TAU          71   // thickness
#define CLR_MU           72   // friction
#define CLR_FRICMODEL    73   // u
#define CLR_BACK         74   // backDepth
#define CLR_COLLERP      75   // u
#define CLR_TRACK        76   // u
#define CLR_REACQ        77   // u
#define CLR_REACQDEPTH   78
#define CLR_MARGIN       79
#define CLR_HANDCOL      80   // u
#define CLR_HANDTH       81
#define CLR_COLLEVERY    82   // u
#define CLR_FLIP         83   // +1 / -1
#define CLR_EDGENORMAL   84   // u
#define CLR_CELL         85
#define CLR_ORDER        86   // u: 0 collision last, 1 tether last
#define CLR_EDGEPASSES   87   // u
#define CLR_BENDPASSES   88   // u
#define CLR_BENDMODEL    89   // u
#define CLR_TARGETLERP   90   // u
#define CLR_DAMPING      91   // damping (> 0: damp)
#define CLR_DAMPREL      92   // u
#define CLR_ADPERSUB     93   // u
#define CLR_TETHERSCALE  94
#define CLR_MAXDSCALE    95
#define CLR_CONE         96   // u: flare cap on
#define CLR_INERTIA      97   // u
#define CLR_ROTVEL       98   // u
#define CLR_AERO         99   // u
#define CLR_RHO          100
#define CLR_CD           101
#define CLR_CL           102
#define CLR_RESETPROJ    103  // u
#define CLR_RESETRE      104  // u: reacquire in the reset projection
#define CLR_G            105  // 12: rel = G x (rows 0..2, translation in [3], [7], [11])
#define CLR_CAPOLD       117  // CL_MAX_CAPS*6
#define CLR_CAPNEW       213  // CL_MAX_CAPS*6
#define CLR_SIDED        309  // u: sidedContacts
#define CLR_SIDEDEPTH    310
#define CLR_CONEBODY     311  // u: coneBody
#define CLR_CONEKR       312  // coneBody: allowance relax factor per substep
#define CLR_DEBUG        313  // u: r_clothGpuSkip (1 contact search, 2 coloured constraints, 4 particle pass, 8 aero, 16 friction pass)
#define CLR_MATS         314  // nBones*16 (row-vector skinning matrices)

// ---- stats (in the frame buffer, CLR_STATS)
#define CLS_CONTACTS     0
#define CLS_HAND         1
#define CLS_CONE         2
#define CLS_TETHERS      3
#define CLS_BAD          4
#define CLS_WORDS        5

// ---- draw record
#define CLD_T            0    // blend from REL[RELA] to REL[RELB]
#define CLD_RELA         1    // u
#define CLD_RELB         2    // u
#define CLD_K            3    // 12: bind = K rel (rows 0..2, translation in [3], [7], [11])
#define CLD_DEBUG        15   // u: r_clothDebug (1 hide the inner shell: render vertices with a negative normal sign, 2 hide the outer)
// r_clothGpuPinSkin: pinned particles skinned with this frame's matrices, as the CPU draw does
#define CLD_A            16   // 12: bind = A model (rows 0..2, translation in [3], [7], [11]): the reference bone's inverse
#define CLD_IU           28   // 1 / unitCm (cm -> glm units)
#define CLD_NB           29   // u: bones whose matrices follow (0 = pinned particles blended like the others)
// r_clothDrawLead 2: the old pair's extrapolation blended into the new pair's over one step
#define CLD_RELZ         30   // u: the state before RELA
#define CLD_S            31   // the fraction of a step since RELB
#define CLD_LEAD2        32   // u: 1 = (1-s)(A + (1+s)(A-Z)) + s(B + s(B-A)); 0 = A + t(B-A)
#define CLD_MATS         33   // NB*16 (row-vector skinning matrices of this frame, cm)
#define CLD_WORDS        33   // the fixed part; a record is CLD_WORDS + NB*16 words
