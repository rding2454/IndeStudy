/**CFile****************************************************************

  FileName    [rwtUtil.c]

  SystemName  [ABC: Logic synthesis and verification system.]

  PackageName [DAG-aware AIG rewriting package.]

  Synopsis    [Various utilities.]

  Author      [Alan Mishchenko]
  
  Affiliation [UC Berkeley]

  Date        [Ver. 1.0. Started - June 20, 2005.]

  Revision    [$Id: rwtUtil.c,v 1.00 2005/06/20 00:00:00 alanmi Exp $]

***********************************************************************/

#include "rwt.h"

////////////////////////////////////////////////////////////////////////
///                        DECLARATIONS                              ///
////////////////////////////////////////////////////////////////////////

// precomputed data
static unsigned short s_RwtPracticalClasses[];
static unsigned short s_RwtAigSubgraphs[];

////////////////////////////////////////////////////////////////////////
///                     FUNCTION DEFINITIONS                         ///
////////////////////////////////////////////////////////////////////////

/**Function*************************************************************

  Synopsis    [Adds the node to the end of the list.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Rwt_ListAddToTail( Rwt_Node_t ** ppList, Rwt_Node_t * pNode )
{
    Rwt_Node_t * pTemp;
    // find the last one
    for ( pTemp = *ppList; pTemp; pTemp = pTemp->pNext )
        ppList = &pTemp->pNext;
    // attach at the end
    *ppList = pNode;
}

/**Function*************************************************************

  Synopsis    [Adds one node.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Rwt_Node_t * Rwt_ManAddVar( Rwt_Man_t * p, unsigned uTruth, int fPrecompute )
{
    Rwt_Node_t * pNew;
    pNew = (Rwt_Node_t *)Mem_FixedEntryFetch( p->pMmNode );
    pNew->Id     = p->vForest->nSize;
    pNew->TravId = 0;
    pNew->uTruth = uTruth;
    pNew->Level  = 0;
    pNew->Volume = 0;
    pNew->fUsed  = 1;
    pNew->fExor  = 0;
    pNew->p0     = NULL;
    pNew->p1     = NULL;    
    pNew->pNext  = NULL;
    Vec_PtrPush( p->vForest, pNew );
    if ( fPrecompute )
        Rwt_ListAddToTail( p->pTable + uTruth, pNew );
    return pNew;
}

/**Function*************************************************************

  Synopsis    [Adds one node.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
Rwt_Node_t * Rwt_ManAddNode( Rwt_Man_t * p, Rwt_Node_t * p0, Rwt_Node_t * p1, int fExor, int Level, int Volume )
{
    Rwt_Node_t * pNew;
    unsigned uTruth;
    // compute truth table, leve, volume
    p->nConsidered++;
    if ( fExor )
        uTruth = (p0->uTruth ^ p1->uTruth);
    else
        uTruth = (Rwt_IsComplement(p0)? ~Rwt_Regular(p0)->uTruth : Rwt_Regular(p0)->uTruth) & 
                 (Rwt_IsComplement(p1)? ~Rwt_Regular(p1)->uTruth : Rwt_Regular(p1)->uTruth) & 0xFFFF;
    // create the new node
    pNew = (Rwt_Node_t *)Mem_FixedEntryFetch( p->pMmNode );
    pNew->Id     = p->vForest->nSize;
    pNew->TravId = 0;
    pNew->uTruth = uTruth;
    pNew->Level  = Level;
    pNew->Volume = Volume;
    pNew->fUsed  = 0;
    pNew->fExor  = fExor;
    pNew->p0     = p0;
    pNew->p1     = p1;
    pNew->pNext  = NULL;
    Vec_PtrPush( p->vForest, pNew );
    // do not add if the node is not essential
    if ( uTruth != p->puCanons[uTruth] )
        return pNew;

    // add to the list
    p->nAdded++;
    if ( p->pTable[uTruth] == NULL )
        p->nClasses++;
    Rwt_ListAddToTail( p->pTable + uTruth, pNew );
    return pNew;
}

/**Function*************************************************************

  Synopsis    [Adds one node.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Rwt_Trav_rec( Rwt_Man_t * p, Rwt_Node_t * pNode, int * pVolume )
{
    if ( pNode->fUsed || pNode->TravId == p->nTravIds )
        return;
    pNode->TravId = p->nTravIds;
    (*pVolume)++;
    if ( pNode->fExor )
        (*pVolume)++;
    Rwt_Trav_rec( p, Rwt_Regular(pNode->p0), pVolume );
    Rwt_Trav_rec( p, Rwt_Regular(pNode->p1), pVolume );
}

/**Function*************************************************************

  Synopsis    [Adds one node.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Rwt_ManIncTravId( Rwt_Man_t * p )
{
    Rwt_Node_t * pNode;
    int i;
    if ( p->nTravIds++ < 0x8FFFFFFF )
        return;
    Vec_PtrForEachEntry( p->vForest, pNode, i )
        pNode->TravId = 0;
    p->nTravIds = 1;
}

/**Function*************************************************************

  Synopsis    [Adds one node.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int Rwt_ManNodeVolume( Rwt_Man_t * p, Rwt_Node_t * p0, Rwt_Node_t * p1 )
{
    int Volume = 0;
    Rwt_ManIncTravId( p );
    Rwt_Trav_rec( p, p0, &Volume );
    Rwt_Trav_rec( p, p1, &Volume );
    return Volume;
}

/**Function*************************************************************

  Synopsis    [Loads data.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void Rwt_ManLoadFromArray( Rwt_Man_t * p, int fVerbose )
{
    unsigned short * pArray = s_RwtAigSubgraphs;
    Rwt_Node_t * p0, * p1;
    unsigned Entry0, Entry1;
    int Level, Volume, nEntries, fExor;
    int i, clk = clock();

    // reconstruct the forest
    for ( i = 0; ; i++ )
    {
        Entry0 = pArray[2*i + 0];
        Entry1 = pArray[2*i + 1];
        if ( Entry0 == 0 && Entry1 == 0 )
            break;
        // get EXOR flag
        fExor = (Entry0 & 1);
        Entry0 >>= 1;
        // get the nodes
        p0 = p->vForest->pArray[Entry0 >> 1];
        p1 = p->vForest->pArray[Entry1 >> 1];
        // compute the level and volume of the new nodes
        Level  = 1 + RWT_MAX( p0->Level, p1->Level );
        Volume = 1 + Rwt_ManNodeVolume( p, p0, p1 );
        // set the complemented attributes
        p0 = Rwt_NotCond( p0, (Entry0 & 1) );
        p1 = Rwt_NotCond( p1, (Entry1 & 1) );
        // add the node
//        Rwt_ManTryNode( p, p0, p1, Level, Volume );
        Rwt_ManAddNode( p, p0, p1, fExor, Level, Volume + fExor );
    }
    nEntries = i - 1;
    if ( fVerbose )
    {
        printf( "The number of classes = %d. Canonical nodes = %d.\n", p->nClasses, p->nAdded );
        printf( "The number of nodes loaded = %d.  ", nEntries );  PRT( "Loading", clock() - clk );
    }
}

/**Function*************************************************************

  Synopsis    [Create practical classes.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
char * Rwt_ManGetPractical( Rwt_Man_t * p )
{
    char * pPractical;
    int i;
    pPractical = ALLOC( char, p->nFuncs );
    memset( pPractical, 0, sizeof(char) * p->nFuncs );
    pPractical[0] = 1;
    for ( i = 1; ; i++ )
    {
        if ( s_RwtPracticalClasses[i] == 0 )
            break;
        pPractical[ s_RwtPracticalClasses[i] ] = 1;
    }
    return pPractical;
}

////////////////////////////////////////////////////////////////////////
///                       END OF FILE                                ///
////////////////////////////////////////////////////////////////////////

// the following 135 practical NPN classes of 4-variable functions were computed
// by considering all 4-input cuts appearing in IWLS, MCNC, and ISCAS benchmarks
static unsigned short s_RwtPracticalClasses[] = 
{
    0x0000, 0x0001, 0x0003, 0x0006, 0x0007, 0x000f, 0x0016, 0x0017, 0x0018, 0x0019, 0x001b, 
    0x001e, 0x001f, 0x003c, 0x003d, 0x003f, 0x0069, 0x006b, 0x006f, 0x007e, 0x007f, 0x00ff, 
    0x0116, 0x0118, 0x0119, 0x011a, 0x011b, 0x011e, 0x011f, 0x012c, 0x012d, 0x012f, 0x013c, 
    0x013d, 0x013e, 0x013f, 0x0168, 0x0169, 0x016f, 0x017f, 0x0180, 0x0181, 0x0182, 0x0183, 
    0x0186, 0x0189, 0x018b, 0x018f, 0x0198, 0x0199, 0x019b, 0x01a8, 0x01a9, 0x01aa, 0x01ab, 
    0x01ac, 0x01ad, 0x01ae, 0x01af, 0x01bf, 0x01e9, 0x01ea, 0x01eb, 0x01ee, 0x01ef, 0x01fe, 
    0x033c, 0x033d, 0x033f, 0x0356, 0x0357, 0x0358, 0x0359, 0x035a, 0x035b, 0x035f, 0x0368, 
    0x0369, 0x036c, 0x036e, 0x037d, 0x03c0, 0x03c1, 0x03c3, 0x03c7, 0x03cf, 0x03d4, 0x03d5, 
    0x03d7, 0x03d8, 0x03d9, 0x03dc, 0x03dd, 0x03de, 0x03fc, 0x0660, 0x0661, 0x0666, 0x0669, 
    0x066f, 0x0676, 0x067e, 0x0690, 0x0696, 0x0697, 0x069f, 0x06b1, 0x06b6, 0x06f0, 0x06f2, 
    0x06f6, 0x06f9, 0x0776, 0x0778, 0x07b0, 0x07b1, 0x07b4, 0x07bc, 0x07f0, 0x07f2, 0x07f8, 
    0x0ff0, 0x1683, 0x1696, 0x1698, 0x169e, 0x16e9, 0x178e, 0x17e8, 0x18e7, 0x19e6, 0x1be4, 
    0x1ee1, 0x3cc3, 0x6996, 0x0000
};

static unsigned short s_RwtAigSubgraphs[] = 
{
    0x0008,0x0002, 0x000a,0x0002, 0x0008,0x0003, 0x000a,0x0003, 0x0009,0x0002, 
    0x000c,0x0002, 0x000e,0x0002, 0x000c,0x0003, 0x000e,0x0003, 0x000d,0x0002, 
    0x000c,0x0004, 0x000e,0x0004, 0x000c,0x0005, 0x000e,0x0005, 0x000d,0x0004, 
    0x0010,0x0002, 0x0012,0x0002, 0x0010,0x0003, 0x0012,0x0003, 0x0011,0x0002, 
    0x0010,0x0004, 0x0012,0x0004, 0x0010,0x0005, 0x0012,0x0005, 0x0011,0x0004, 
    0x0010,0x0006, 0x0012,0x0006, 0x0010,0x0007, 0x0012,0x0007, 0x0011,0x0006, 
    0x0016,0x0005, 0x0014,0x0006, 0x0016,0x0006, 0x0014,0x0007, 0x0016,0x0007, 
    0x0015,0x0006, 0x0014,0x0008, 0x0016,0x0008, 0x0014,0x0009, 0x0016,0x0009, 
    0x0015,0x0008, 0x0018,0x0006, 0x001a,0x0006, 0x0018,0x0007, 0x001a,0x0007, 
    0x0019,0x0006, 0x0018,0x0009, 0x001a,0x0009, 0x0019,0x0008, 0x001e,0x0005, 
    0x001c,0x0006, 0x001e,0x0006, 0x001c,0x0007, 0x001e,0x0007, 0x001d,0x0006, 
    0x001c,0x0008, 0x001e,0x0008, 0x001c,0x0009, 0x001e,0x0009, 0x001d,0x0008, 
    0x0020,0x0006, 0x0022,0x0006, 0x0020,0x0007, 0x0022,0x0007, 0x0021,0x0006, 
    0x0020,0x0008, 0x0022,0x0008, 0x0020,0x0009, 0x0022,0x0009, 0x0021,0x0008, 
    0x0024,0x0006, 0x0026,0x0006, 0x0024,0x0007, 0x0026,0x0007, 0x0025,0x0006, 
    0x0026,0x0008, 0x0024,0x0009, 0x0026,0x0009, 0x0025,0x0008, 0x0028,0x0004, 
    0x002a,0x0004, 0x0028,0x0005, 0x002a,0x0007, 0x0028,0x0008, 0x002a,0x0009, 
    0x0029,0x0008, 0x002a,0x000b, 0x0029,0x000a, 0x002a,0x000f, 0x0029,0x000e, 
    0x002a,0x0011, 0x002a,0x0013, 0x002c,0x0004, 0x002e,0x0004, 0x002c,0x0005, 
    0x002c,0x0009, 0x002e,0x0009, 0x002d,0x0008, 0x002d,0x000c, 0x002e,0x000f, 
    0x002e,0x0011, 0x002e,0x0012, 0x0030,0x0004, 0x0032,0x0007, 0x0032,0x0009, 
    0x0031,0x0008, 0x0032,0x000b, 0x0032,0x000d, 0x0032,0x000f, 0x0031,0x000e, 
    0x0032,0x0013, 0x0034,0x0004, 0x0036,0x0004, 0x0034,0x0005, 0x0036,0x0005, 
    0x0035,0x0004, 0x0036,0x0008, 0x0034,0x0009, 0x0036,0x0009, 0x0035,0x0008, 
    0x0036,0x000b, 0x0036,0x000d, 0x0036,0x0011, 0x0035,0x0010, 0x0036,0x0013, 
    0x0038,0x0004, 0x0039,0x0004, 0x0038,0x0009, 0x003a,0x0009, 0x0039,0x0008, 
    0x0038,0x000b, 0x003a,0x000b, 0x003a,0x000d, 0x003a,0x0011, 0x003a,0x0012, 
    0x0038,0x0013, 0x003a,0x0013, 0x003c,0x0002, 0x003e,0x0002, 0x003c,0x0003, 
    0x003e,0x0005, 0x003e,0x0007, 0x003c,0x0008, 0x003e,0x0008, 0x003c,0x0009, 
    0x003e,0x0009, 0x003d,0x0008, 0x003e,0x000d, 0x003e,0x0011, 0x003e,0x0013, 
    0x003e,0x0017, 0x003e,0x001b, 0x003e,0x001d, 0x0040,0x0002, 0x0042,0x0002, 
    0x0042,0x0005, 0x0041,0x0006, 0x0042,0x0008, 0x0041,0x0008, 0x0042,0x000d, 
    0x0042,0x0011, 0x0042,0x0015, 0x0042,0x0019, 0x0042,0x001b, 0x0042,0x001c, 
    0x0041,0x001c, 0x0044,0x0002, 0x0046,0x0003, 0x0045,0x0004, 0x0046,0x0007, 
    0x0045,0x0008, 0x0046,0x000b, 0x0046,0x000f, 0x0046,0x0013, 0x0045,0x0012, 
    0x0046,0x0017, 0x0046,0x001b, 0x0046,0x0021, 0x0048,0x0002, 0x004a,0x0002, 
    0x0048,0x0003, 0x004a,0x0003, 0x0049,0x0002, 0x0048,0x0008, 0x004a,0x0008, 
    0x0048,0x0009, 0x004a,0x0009, 0x0049,0x0008, 0x004a,0x000b, 0x004a,0x000f, 
    0x004a,0x0011, 0x004a,0x0012, 0x004a,0x0013, 0x004a,0x0015, 0x004a,0x0019, 
    0x004a,0x001b, 0x004a,0x001d, 0x004c,0x0002, 0x004c,0x0003, 0x004d,0x0002, 
    0x004c,0x0008, 0x004e,0x0008, 0x004c,0x0009, 0x004e,0x0009, 0x004d,0x0008, 
    0x004c,0x000b, 0x004e,0x000b, 0x004c,0x000f, 0x004e,0x000f, 0x004e,0x0011, 
    0x004c,0x0012, 0x004c,0x0013, 0x004e,0x0013, 0x004e,0x0015, 0x004c,0x0017, 
    0x004e,0x0019, 0x004c,0x001b, 0x004e,0x001b, 0x004c,0x001c, 0x004c,0x001d, 
    0x004e,0x001d, 0x0050,0x0004, 0x0052,0x0004, 0x0050,0x0006, 0x0052,0x0009, 
    0x0052,0x000d, 0x0052,0x000f, 0x0052,0x0013, 0x0052,0x0017, 0x0052,0x0019, 
    0x0052,0x001d, 0x0052,0x001f, 0x0052,0x0021, 0x0052,0x0023, 0x0052,0x0024, 
    0x0052,0x0025, 0x0051,0x0024, 0x0052,0x0027, 0x0054,0x0004, 0x0056,0x0004, 
    0x0054,0x0005, 0x0056,0x0006, 0x0054,0x0007, 0x0056,0x0011, 0x0056,0x001b, 
    0x0056,0x001e, 0x0054,0x001f, 0x0056,0x001f, 0x0056,0x0020, 0x0054,0x0021, 
    0x0055,0x0020, 0x0056,0x0024, 0x0054,0x0025, 0x0056,0x0025, 0x0055,0x0024, 
    0x0054,0x0027, 0x0056,0x0027, 0x0055,0x0026, 0x005a,0x0007, 0x005a,0x0009, 
    0x005a,0x000b, 0x005a,0x0015, 0x005a,0x001f, 0x0059,0x0020, 0x0058,0x0024, 
    0x005a,0x0024, 0x005a,0x0027, 0x0059,0x0026, 0x005c,0x0004, 0x005e,0x0004, 
    0x005c,0x0005, 0x005e,0x0006, 0x005c,0x0007, 0x005d,0x0006, 0x005e,0x000d, 
    0x005e,0x0013, 0x005e,0x0017, 0x005c,0x001f, 0x005d,0x001e, 0x005e,0x0020, 
    0x005e,0x0021, 0x005e,0x0022, 0x005e,0x0023, 0x005c,0x0024, 0x005e,0x0024, 
    0x005c,0x0025, 0x005e,0x0025, 0x005d,0x0024, 0x005e,0x0026, 0x005e,0x0027, 
    0x0062,0x0004, 0x0061,0x0004, 0x0062,0x0006, 0x0061,0x0006, 0x0060,0x000f, 
    0x0060,0x0013, 0x0062,0x0013, 0x0060,0x0019, 0x0062,0x001c, 0x0060,0x001d, 
    0x0062,0x001d, 0x0062,0x001f, 0x0060,0x0021, 0x0060,0x0023, 0x0062,0x0024, 
    0x0060,0x0027, 0x0061,0x0026, 0x0064,0x0002, 0x0066,0x0002, 0x0064,0x0006, 
    0x0066,0x0007, 0x0066,0x0009, 0x0066,0x000d, 0x0066,0x0013, 0x0066,0x0015, 
    0x0066,0x0017, 0x0066,0x0019, 0x0066,0x001a, 0x0065,0x001a, 0x0066,0x001f, 
    0x0066,0x0023, 0x0066,0x0027, 0x0066,0x002f, 0x0066,0x0030, 0x006a,0x0002, 
    0x0068,0x0003, 0x0068,0x0006, 0x006a,0x0006, 0x006a,0x0011, 0x0068,0x0016, 
    0x0068,0x0017, 0x006a,0x0017, 0x006a,0x001a, 0x006a,0x001b, 0x006a,0x0025, 
    0x006a,0x002d, 0x006e,0x0003, 0x006e,0x0007, 0x006e,0x0009, 0x006e,0x000b, 
    0x006e,0x0015, 0x006e,0x0016, 0x006e,0x0017, 0x006c,0x001a, 0x006e,0x001a, 
    0x006e,0x001f, 0x006e,0x002b, 0x006e,0x0035, 0x0070,0x0002, 0x0070,0x0003, 
    0x0072,0x0006, 0x0070,0x0007, 0x0071,0x0006, 0x0072,0x000b, 0x0072,0x000f, 
    0x0072,0x0013, 0x0070,0x0015, 0x0071,0x0014, 0x0072,0x0017, 0x0072,0x0018, 
    0x0070,0x0019, 0x0072,0x0019, 0x0070,0x001a, 0x0070,0x001b, 0x0072,0x001b, 
    0x0071,0x001a, 0x0072,0x0021, 0x0072,0x0029, 0x0076,0x0002, 0x0076,0x0003, 
    0x0075,0x0002, 0x0076,0x0006, 0x0074,0x0007, 0x0076,0x0007, 0x0075,0x0006, 
    0x0076,0x000d, 0x0076,0x0011, 0x0076,0x0013, 0x0075,0x0014, 0x0076,0x0019, 
    0x0076,0x001a, 0x0076,0x001b, 0x0075,0x001c, 0x0074,0x0023, 0x0075,0x0022, 
    0x0074,0x0026, 0x0076,0x0026, 0x0074,0x0027, 0x0076,0x002b, 0x0076,0x002f, 
    0x0078,0x0002, 0x0078,0x0004, 0x007a,0x0004, 0x007a,0x0005, 0x0079,0x0004, 
    0x007a,0x0009, 0x007a,0x000a, 0x007a,0x000b, 0x007a,0x000d, 0x007a,0x000f, 
    0x007a,0x0010, 0x007a,0x0011, 0x007a,0x0012, 0x007a,0x0013, 0x007a,0x0017, 
    0x007a,0x001b, 0x007a,0x0021, 0x007a,0x0027, 0x007a,0x002b, 0x007a,0x002f, 
    0x007a,0x0030, 0x0079,0x0034, 0x007a,0x0039, 0x007a,0x003a, 0x007e,0x0002, 
    0x007c,0x0004, 0x007e,0x0004, 0x007e,0x000c, 0x007c,0x000d, 0x007e,0x0011, 
    0x007e,0x0013, 0x007e,0x001b, 0x007e,0x0025, 0x007e,0x002d, 0x007e,0x0037, 
    0x0082,0x0003, 0x0082,0x0005, 0x0082,0x0009, 0x0082,0x000b, 0x0080,0x0010, 
    0x0082,0x0010, 0x0082,0x0012, 0x0082,0x0015, 0x0082,0x001f, 0x0082,0x002b, 
    0x0082,0x0035, 0x0082,0x0039, 0x0082,0x003f, 0x0084,0x0002, 0x0086,0x0002, 
    0x0084,0x0003, 0x0086,0x0003, 0x0085,0x0002, 0x0086,0x0004, 0x0084,0x0005, 
    0x0085,0x0004, 0x0086,0x000a, 0x0084,0x000b, 0x0085,0x000a, 0x0086,0x000d, 
    0x0086,0x000e, 0x0086,0x000f, 0x0084,0x0010, 0x0084,0x0011, 0x0086,0x0011, 
    0x0085,0x0010, 0x0084,0x0012, 0x0084,0x0013, 0x0086,0x0013, 0x0085,0x0012, 
    0x0086,0x0019, 0x0086,0x0023, 0x0086,0x0029, 0x0086,0x0033, 0x0086,0x0039, 
    0x008a,0x0003, 0x0089,0x0002, 0x0088,0x0004, 0x008a,0x0004, 0x0088,0x0005, 
    0x0089,0x0004, 0x008a,0x000b, 0x008a,0x0010, 0x0088,0x0011, 0x008a,0x0011, 
    0x0089,0x0010, 0x0088,0x0012, 0x008a,0x0012, 0x0089,0x0012, 0x008a,0x0017, 
    0x008a,0x001b, 0x0089,0x0020, 0x008a,0x0025, 0x0088,0x0027, 0x008a,0x002b, 
    0x008a,0x002f, 0x008a,0x0039, 0x0088,0x003a, 0x008d,0x0044, 0x0092,0x0009, 
    0x0092,0x0025, 0x0092,0x0029, 0x0092,0x002d, 0x0092,0x0033, 0x0092,0x0037, 
    0x0092,0x003d, 0x0092,0x0041, 0x0095,0x0002, 0x0095,0x0004, 0x0095,0x0010, 
    0x0095,0x0012, 0x0096,0x0021, 0x0096,0x0029, 0x0095,0x002e, 0x0096,0x0030, 
    0x0096,0x0033, 0x0096,0x003a, 0x0096,0x0043, 0x009a,0x0008, 0x009a,0x0009, 
    0x0099,0x0008, 0x009a,0x0011, 0x009a,0x0023, 0x009a,0x0033, 0x009a,0x003d, 
    0x009a,0x0044, 0x009a,0x0045, 0x0099,0x0044, 0x009d,0x0002, 0x009e,0x0008, 
    0x009c,0x0009, 0x009e,0x0009, 0x009d,0x0008, 0x009e,0x0011, 0x009d,0x0010, 
    0x009e,0x001f, 0x009e,0x003f, 0x00a0,0x0009, 0x00a0,0x0011, 0x00a2,0x0030, 
    0x00a2,0x0033, 0x00a6,0x0006, 0x00a6,0x0007, 0x00a6,0x0011, 0x00a6,0x0044, 
    0x00a6,0x004b, 0x00aa,0x0007, 0x00aa,0x0015, 0x00ae,0x0006, 0x00ae,0x0011, 
    0x00ae,0x001b, 0x00ae,0x0025, 0x00ae,0x003d, 0x00ae,0x0041, 0x00ae,0x0043, 
    0x00ae,0x0045, 0x00b2,0x0006, 0x00b0,0x0007, 0x00b1,0x0006, 0x00b2,0x0017, 
    0x00b1,0x0016, 0x00b0,0x0019, 0x00b2,0x0021, 0x00b2,0x003d, 0x00b5,0x004a, 
    0x00ba,0x0009, 0x00ba,0x000f, 0x00bc,0x0009, 0x00be,0x0009, 0x00be,0x000f, 
    0x00bd,0x000e, 0x00be,0x0017, 0x00c2,0x0009, 0x00c2,0x0019, 0x00c2,0x001f, 
    0x00c2,0x0033, 0x00c6,0x0009, 0x00c5,0x000e, 0x00c6,0x0015, 0x00c6,0x0023, 
    0x00c4,0x002d, 0x00c6,0x002f, 0x00c5,0x002e, 0x00c6,0x0045, 0x00ce,0x0007, 
    0x00ce,0x0021, 0x00ce,0x0023, 0x00ce,0x0025, 0x00ce,0x0027, 0x00ce,0x0033, 
    0x00ce,0x003d, 0x00d2,0x0006, 0x00d0,0x0015, 0x00d0,0x001b, 0x00d2,0x001b, 
    0x00d1,0x001a, 0x00d0,0x001f, 0x00d2,0x0025, 0x00d1,0x0024, 0x00d2,0x0037, 
    0x00d2,0x0041, 0x00d2,0x0045, 0x00d9,0x0044, 0x00e1,0x0004, 0x00e2,0x000d, 
    0x00e2,0x0021, 0x00e0,0x003a, 0x00e6,0x003d, 0x00e6,0x0061, 0x00e6,0x0067, 
    0x00e9,0x0004, 0x00ea,0x0008, 0x00ea,0x0009, 0x00ea,0x0039, 0x00e9,0x0038, 
    0x00ea,0x003f, 0x00ec,0x000d, 0x00ee,0x000d, 0x00ee,0x0037, 0x00f2,0x003d, 
    0x00f2,0x0062, 0x00f5,0x0002, 0x00fa,0x0017, 0x00fa,0x003d, 0x00fe,0x0006, 
    0x00fd,0x0006, 0x00fc,0x0015, 0x00fe,0x001b, 0x00fc,0x0025, 0x00fe,0x0025, 
    0x00fd,0x0024, 0x00fe,0x0041, 0x00fe,0x004d, 0x00fd,0x004e, 0x0101,0x0014, 
    0x0106,0x004d, 0x010a,0x0009, 0x010a,0x000b, 0x0109,0x000a, 0x010a,0x004f, 
    0x010a,0x0058, 0x010e,0x0008, 0x010c,0x0009, 0x010e,0x0009, 0x010d,0x0008, 
    0x010e,0x000b, 0x010e,0x002b, 0x010d,0x002a, 0x010e,0x0035, 0x010e,0x003d, 
    0x010e,0x003f, 0x010e,0x0049, 0x010e,0x0057, 0x010d,0x0056, 0x010d,0x0058, 
    0x0111,0x0004, 0x0111,0x0006, 0x0110,0x0009, 0x0112,0x0009, 0x0111,0x0008, 
    0x0112,0x002f, 0x0110,0x0035, 0x0110,0x0037, 0x0112,0x0039, 0x0112,0x003d, 
    0x0112,0x003f, 0x0112,0x0045, 0x0111,0x0044, 0x0112,0x004b, 0x0112,0x0059, 
    0x0112,0x0069, 0x0112,0x007f, 0x0116,0x0009, 0x0115,0x0008, 0x0114,0x000b, 
    0x0116,0x000b, 0x0116,0x0058, 0x011a,0x0015, 0x011a,0x001f, 0x011a,0x002b, 
    0x011a,0x003f, 0x011a,0x0049, 0x011a,0x0085, 0x011e,0x0007, 0x011e,0x0019, 
    0x011e,0x001b, 0x011e,0x0023, 0x011e,0x0027, 0x011e,0x002f, 0x011e,0x0043, 
    0x011e,0x004b, 0x011e,0x004e, 0x011e,0x004f, 0x011e,0x005f, 0x011e,0x0061, 
    0x011e,0x0065, 0x011e,0x0083, 0x0122,0x0006, 0x0120,0x0007, 0x0122,0x0007, 
    0x0121,0x0006, 0x0122,0x0049, 0x0121,0x004e, 0x0122,0x008f, 0x0125,0x0004, 
    0x0124,0x0007, 0x0125,0x0006, 0x0124,0x001b, 0x0126,0x001b, 0x0126,0x0045, 
    0x0126,0x0087, 0x0128,0x0007, 0x0129,0x0006, 0x012a,0x0019, 0x012a,0x003d, 
    0x012a,0x0051, 0x012a,0x0065, 0x012a,0x0083, 0x012d,0x005a, 0x0132,0x0009, 
    0x0132,0x008f, 0x0134,0x0009, 0x0135,0x003e, 0x013a,0x003d, 0x013a,0x0044, 
    0x0139,0x0044, 0x013e,0x0009, 0x013d,0x0008, 0x013c,0x003d, 0x013c,0x0044, 
    0x013c,0x0053, 0x013e,0x008f, 0x013e,0x0095, 0x0142,0x0044, 0x0142,0x0097, 
    0x0142,0x009e, 0x0144,0x0007, 0x0148,0x0015, 0x0148,0x001c, 0x0148,0x001f, 
    0x0148,0x0026, 0x0149,0x0086, 0x014d,0x0006, 0x014e,0x0044, 0x014d,0x0048, 
    0x014e,0x009e, 0x0152,0x0009, 0x0151,0x00a6, 0x0155,0x0030, 0x015d,0x003a, 
    0x0162,0x009e, 0x0164,0x000f, 0x0164,0x0013, 0x0169,0x000e, 0x0174,0x0009, 
    0x0179,0x0008, 0x0180,0x0009, 0x0181,0x0044, 0x0186,0x0044, 0x0185,0x0044, 
    0x018a,0x0068, 0x0195,0x004e, 0x01a6,0x0009, 0x01a5,0x0008, 0x01b1,0x003a, 
    0x01c4,0x0029, 0x01c4,0x0030, 0x01ca,0x008f, 0x01ca,0x0095, 0x01cc,0x0029, 
    0x01cc,0x0033, 0x01ce,0x003d, 0x01d6,0x00b2, 0x01d8,0x0009, 0x01d9,0x002a, 
    0x01d9,0x0056, 0x01d9,0x00a4, 0x01dd,0x003a, 0x01e2,0x00b2, 0x01e6,0x0013, 
    0x01e6,0x009f, 0x01e6,0x00ba, 0x01e6,0x00c0, 0x01e6,0x00d3, 0x01e6,0x00d5, 
    0x01e6,0x00e5, 0x01e8,0x0005, 0x01f2,0x0013, 0x01f2,0x0095, 0x01f2,0x009f, 
    0x01f2,0x00ba, 0x01f2,0x00c0, 0x01f2,0x00d3, 0x0202,0x008f, 0x0202,0x0095, 
    0x0202,0x00f3, 0x0202,0x00f9, 0x020a,0x0044, 0x0209,0x00b4, 0x020e,0x0009, 
    0x020d,0x0008, 0x020c,0x003d, 0x020c,0x0044, 0x020c,0x0053, 0x020e,0x008f, 
    0x020e,0x0095, 0x020c,0x00b1, 0x020e,0x00f3, 0x020e,0x00f9, 0x0210,0x0013, 
    0x0211,0x0024, 0x0210,0x0026, 0x0219,0x0004, 0x021e,0x008f, 0x021e,0x0095, 
    0x0221,0x003a, 0x0230,0x0009, 0x0236,0x0009, 0x0234,0x0029, 0x0234,0x0030, 
    0x0234,0x0033, 0x0234,0x003a, 0x0234,0x003d, 0x0234,0x0044, 0x0235,0x00a6, 
    0x023a,0x0009, 0x023d,0x003a, 0x0245,0x0044, 0x0249,0x003a, 0x024e,0x009e, 
    0x024e,0x0106, 0x0251,0x0026, 0x0258,0x0013, 0x0259,0x0024, 0x0258,0x0061, 
    0x0259,0x0086, 0x0258,0x00c7, 0x0258,0x00df, 0x0259,0x00ec, 0x0258,0x00fc, 
    0x025d,0x0024, 0x025d,0x00de, 0x0260,0x00f6, 0x0268,0x0009, 0x0269,0x0044, 
    0x0268,0x00f3, 0x0268,0x00f9, 0x026d,0x003a, 0x0270,0x0068, 0x0275,0x003a, 
    0x027a,0x0044, 0x0279,0x0044, 0x027e,0x007e, 0x0281,0x0044, 0x0285,0x0008, 
    0x028d,0x0006, 0x028d,0x00d2, 0x0295,0x00cc, 0x0296,0x00f6, 0x0295,0x00f8, 
    0x0299,0x0030, 0x029e,0x007e, 0x029d,0x0080, 0x02a6,0x008f, 0x02a6,0x0095, 
    0x02aa,0x0029, 0x02aa,0x0030, 0x02b5,0x0008, 0x02b9,0x003a, 0x02bd,0x0004, 
    0x02bd,0x00fc, 0x02c2,0x00b2, 0x02c1,0x00b4, 0x02c4,0x0029, 0x02c8,0x0029, 
    0x02c8,0x0033, 0x02ca,0x003d, 0x02ce,0x0029, 0x02ce,0x0030, 0x02d2,0x0068, 
    0x02d1,0x006a, 0x02d5,0x006a, 0x02d9,0x0008, 0x02de,0x012c, 0x02e2,0x012c, 
    0x02e4,0x0009, 0x02e5,0x002a, 0x02e5,0x0056, 0x02e5,0x012c, 0x02ea,0x0029, 
    0x02ea,0x0030, 0x02e9,0x0030, 0x02ec,0x0029, 0x02ec,0x0030, 0x02ee,0x012c, 
    0x02f1,0x0068, 0x02f1,0x00b2, 0x02f1,0x0108, 0x02f1,0x012c, 0x02f6,0x0013, 
    0x02f6,0x0015, 0x02f6,0x001f, 0x02f6,0x0030, 0x02f6,0x0065, 0x02f6,0x0067, 
    0x02f6,0x009f, 0x02f6,0x00b6, 0x02f6,0x00b9, 0x02f6,0x00c0, 0x02f6,0x00cf, 
    0x02f6,0x0107, 0x02f6,0x010b, 0x02f6,0x010f, 0x02f6,0x0115, 0x02f6,0x012d, 
    0x02f6,0x0134, 0x02f6,0x0153, 0x02f6,0x0171, 0x02f6,0x0176, 0x02f8,0x0003, 
    0x02fa,0x017b, 0x02fc,0x00ba, 0x02fc,0x00d3, 0x0302,0x0013, 0x0302,0x001f, 
    0x0302,0x0030, 0x0302,0x005d, 0x0302,0x0065, 0x0302,0x0067, 0x0302,0x0099, 
    0x0302,0x009f, 0x0302,0x00ad, 0x0302,0x00b9, 0x0302,0x00c0, 0x0302,0x00cf, 
    0x0301,0x00d2, 0x0301,0x00fe, 0x0302,0x0107, 0x0302,0x010b, 0x0302,0x010f, 
    0x0302,0x0117, 0x0302,0x0134, 0x0302,0x0153, 0x0302,0x0157, 0x0302,0x0176, 
    0x0306,0x0029, 0x0308,0x00b2, 0x0309,0x00dc, 0x030d,0x00f8, 0x0312,0x00f3, 
    0x0318,0x007e, 0x031d,0x0080, 0x0321,0x0008, 0x0321,0x0094, 0x0326,0x017b, 
    0x0326,0x0181, 0x0329,0x012e, 0x032a,0x017b, 0x032a,0x0181, 0x032e,0x008f, 
    0x032e,0x0095, 0x032e,0x00f3, 0x032e,0x00f9, 0x0332,0x0009, 0x0331,0x0008, 
    0x0330,0x003d, 0x0330,0x0044, 0x0330,0x0053, 0x0332,0x008f, 0x0332,0x0095, 
    0x0330,0x00b1, 0x0332,0x00f3, 0x0332,0x00f9, 0x0330,0x0127, 0x0332,0x017b, 
    0x0332,0x0181, 0x033c,0x0013, 0x033c,0x001c, 0x033d,0x0086, 0x033d,0x00ec, 
    0x033d,0x0172, 0x033e,0x019d, 0x0345,0x0002, 0x0344,0x008f, 0x0344,0x00f3, 
    0x034d,0x0030, 0x0352,0x0033, 0x0354,0x0029, 0x0354,0x0030, 0x035a,0x0009, 
    0x035a,0x017b, 0x035a,0x019b, 0x035a,0x01a2, 0x035e,0x0181, 0x0360,0x0009, 
    0x0366,0x0009, 0x0364,0x0029, 0x0364,0x0030, 0x0364,0x0033, 0x0364,0x003a, 
    0x0364,0x003d, 0x0364,0x0044, 0x0369,0x0030, 0x0370,0x0029, 0x0370,0x0030, 
    0x0376,0x0033, 0x037a,0x0009, 0x037a,0x019b, 0x037a,0x01a2, 0x037c,0x0009, 
    0x0382,0x0181, 0x0386,0x0009, 0x0384,0x0029, 0x0384,0x0030, 0x0384,0x0033, 
    0x0384,0x003a, 0x0384,0x003d, 0x0384,0x0044, 0x038a,0x0044, 0x038a,0x009e, 
    0x038a,0x0106, 0x038a,0x0198, 0x038d,0x010e, 0x038d,0x0152, 0x038d,0x0158, 
    0x0392,0x009e, 0x0392,0x0106, 0x0392,0x0198, 0x0395,0x0086, 0x0395,0x009a, 
    0x0395,0x00ec, 0x0395,0x0172, 0x0398,0x014e, 0x0398,0x0175, 0x0398,0x018d, 
    0x039c,0x0023, 0x039c,0x0027, 0x039c,0x00ef, 0x039c,0x0139, 0x039c,0x0168, 
    0x03a0,0x0019, 0x03a0,0x001d, 0x03a0,0x0023, 0x03a0,0x0027, 0x03a1,0x004e, 
    0x03a4,0x0162, 0x03a4,0x0183, 0x03a8,0x0013, 0x03a8,0x0027, 0x03a8,0x0133, 
    0x03a8,0x0148, 0x03a8,0x0181, 0x03ac,0x0013, 0x03ac,0x0027, 0x03b0,0x017b, 
    0x03b0,0x0181, 0x03b4,0x004b, 0x03b4,0x00e0, 0x03b4,0x00fb, 0x03b8,0x000f, 
    0x03b8,0x0013, 0x03b8,0x00ab, 0x03b8,0x00bf, 0x03b8,0x00d0, 0x03bd,0x00da, 
    0x03bd,0x012c, 0x03c8,0x000f, 0x03c8,0x0013, 0x03c8,0x0019, 0x03c8,0x001d, 
    0x03cd,0x0086, 0x03cd,0x00ec, 0x03cd,0x0172, 0x03d2,0x00e0, 0x03d2,0x00ef, 
    0x03d2,0x0112, 0x03d2,0x0139, 0x03d2,0x0168, 0x03d6,0x017b, 0x03d6,0x0181, 
    0x03da,0x0133, 0x03da,0x0148, 0x03e2,0x0023, 0x03e2,0x0027, 0x03e6,0x0027, 
    0x03e6,0x0181, 0x03ee,0x017b, 0x03ee,0x0181, 0x03fe,0x003d, 0x0401,0x012a, 
    0x0401,0x019e, 0x0405,0x01a0, 0x040a,0x000d, 0x040a,0x011f, 0x040a,0x016f, 
    0x040d,0x012a, 0x0412,0x017b, 0x041a,0x0033, 0x041a,0x003d, 0x041a,0x0181, 
    0x0421,0x0086, 0x0421,0x009a, 0x0421,0x00ec, 0x0421,0x0172, 0x042e,0x0205, 
    0x043a,0x0205, 0x043e,0x017b, 0x0442,0x01f5, 0x044c,0x0007, 0x0452,0x0033, 
    0x0452,0x01ce, 0x0452,0x01d0, 0x0452,0x01f1, 0x0452,0x01fb, 0x0452,0x0225, 
    0x0454,0x0005, 0x045a,0x0033, 0x045a,0x0181, 0x045a,0x01ce, 0x045a,0x01d0, 
    0x045a,0x01f1, 0x0469,0x01de, 0x046e,0x0181, 0x047a,0x01ce, 0x047a,0x01f1, 
    0x0485,0x012c, 0x0489,0x012c, 0x0490,0x01d8, 0x0496,0x0033, 0x0496,0x003d, 
    0x0498,0x008f, 0x0498,0x00f3, 0x049e,0x0044, 0x049e,0x0221, 0x04a1,0x0006, 
    0x04a2,0x0044, 0x04a6,0x0221, 0x04a9,0x0004, 0x04ac,0x0027, 0x04b1,0x009a, 
    0x04b6,0x0097, 0x04b8,0x0027, 0x04c6,0x0219, 0x04ca,0x017b, 0x04cc,0x004b, 
    0x04d0,0x00ab, 0x04d6,0x017b, 0x04d8,0x000f, 0x04d8,0x0019, 0x04d8,0x0033, 
    0x04d8,0x003d, 0x04de,0x003d, 0x04de,0x0103, 0x04de,0x018b, 0x04de,0x0231, 
    0x04e2,0x0044, 0x04e2,0x009e, 0x04e2,0x0106, 0x04e2,0x0198, 0x04e5,0x01a4, 
    0x04e5,0x01b6, 0x04ea,0x009e, 0x04ea,0x0106, 0x04ea,0x0198, 0x04ed,0x002e, 
    0x04ed,0x0038, 0x04ed,0x00a2, 0x04f1,0x0086, 0x04f1,0x009a, 0x04f1,0x00ec, 
    0x04f1,0x0172, 0x04f9,0x004e, 0x04f8,0x0229, 0x04f8,0x022d, 0x0500,0x023e, 
    0x0504,0x0217, 0x0510,0x00f3, 0x0514,0x0043, 0x0514,0x004d, 0x0514,0x00c3, 
    0x0514,0x013d, 0x0514,0x0215, 0x0514,0x0232, 0x0515,0x0260, 0x0519,0x002a, 
    0x0518,0x0030, 0x0518,0x0067, 0x0518,0x00c9, 0x0518,0x01eb, 0x0518,0x01ef, 
    0x051c,0x0139, 0x051c,0x0168, 0x0520,0x0027, 0x0526,0x014e, 0x0526,0x0175, 
    0x0526,0x018d, 0x052d,0x0200, 0x0532,0x0021, 0x0532,0x00bf, 0x0532,0x00d0, 
    0x0532,0x0239, 0x0532,0x0266, 0x053d,0x0024, 0x053d,0x00da, 0x054a,0x000f, 
    0x054a,0x00ab, 0x054a,0x023a, 0x054e,0x0043, 0x054e,0x004d, 0x054e,0x00c3, 
    0x054e,0x013d, 0x054e,0x0215, 0x054e,0x0232, 0x054e,0x029d, 0x0552,0x014e, 
    0x0552,0x018d, 0x0556,0x00f3, 0x0556,0x01e4, 0x055a,0x0299, 0x055d,0x0086, 
    0x055d,0x009a, 0x055d,0x00ec, 0x055d,0x0172, 0x0566,0x01dc, 0x0566,0x02a5, 
    0x056d,0x020a, 0x057a,0x003d, 0x057a,0x01d4, 0x057a,0x01f3, 0x0579,0x025e, 
    0x057e,0x0139, 0x057e,0x0168, 0x0581,0x0006, 0x0586,0x017b, 0x0586,0x0181, 
    0x0586,0x028c, 0x0588,0x0007, 0x058e,0x0033, 0x058e,0x008f, 0x058e,0x01d0, 
    0x058e,0x027c, 0x0590,0x0003, 0x0596,0x0033, 0x0596,0x008f, 0x0596,0x0095, 
    0x0596,0x01d0, 0x0596,0x027c, 0x05a2,0x026f, 0x05a5,0x0284, 0x05aa,0x017b, 
    0x05ac,0x0205, 0x05b2,0x008f, 0x05b6,0x017b, 0x05b8,0x01da, 0x05c1,0x0276, 
    0x05c6,0x0248, 0x05c8,0x0247, 0x05c8,0x027e, 0x05cc,0x003d, 0x05cc,0x01d4, 
    0x05cc,0x01f3, 0x05d0,0x014e, 0x05d0,0x018d, 0x05da,0x00f9, 0x05dd,0x0006, 
    0x05de,0x0044, 0x05e5,0x002e, 0x05e6,0x02f1, 0x05ea,0x01d4, 0x05ea,0x01f3, 
    0x05ea,0x022d, 0x05ed,0x0002, 0x05f6,0x0027, 0x05fa,0x0097, 0x05fc,0x003d, 
    0x0602,0x003d, 0x0606,0x00f3, 0x060a,0x0027, 0x060e,0x003d, 0x060e,0x0103, 
    0x060e,0x018b, 0x060e,0x0231, 0x060e,0x02d1, 0x0611,0x01fc, 0x0611,0x0234, 
    0x061a,0x0287, 0x061d,0x0214, 0x0621,0x01d4, 0x062a,0x0027, 0x062a,0x022d, 
    0x062e,0x009e, 0x062e,0x0106, 0x062e,0x0198, 0x0632,0x009e, 0x0632,0x0106, 
    0x0632,0x0198, 0x0639,0x0042, 0x0639,0x00b2, 0x0639,0x0108, 0x063d,0x01f8, 
    0x0641,0x0086, 0x0641,0x009a, 0x0641,0x00ec, 0x0641,0x0172, 0x0645,0x0044, 
    0x0649,0x0042, 0x0648,0x0087, 0x0648,0x00ed, 0x0648,0x0173, 0x0649,0x01a0, 
    0x0648,0x0241, 0x0648,0x026f, 0x0648,0x02df, 0x0648,0x0307, 0x064c,0x023a, 
    0x064c,0x02b3, 0x0651,0x0062, 0x0650,0x0217, 0x0651,0x02ac, 0x0650,0x02d6, 
    0x0655,0x0042, 0x065d,0x0042, 0x0664,0x02b1, 0x0664,0x02ce, 0x0669,0x0238, 
    0x066d,0x002a, 0x066c,0x0039, 0x066d,0x01f6, 0x066c,0x0213, 0x066c,0x022e, 
    0x066d,0x02a2, 0x066c,0x02e1, 0x0671,0x002a, 0x0670,0x0030, 0x0670,0x0067, 
    0x0670,0x00c9, 0x0670,0x01eb, 0x0670,0x01ef, 0x0670,0x02c3, 0x0675,0x0020, 
    0x0678,0x0133, 0x0678,0x0148, 0x067c,0x0027, 0x0681,0x023a, 0x0684,0x0021, 
    0x0684,0x00bf, 0x0684,0x00d0, 0x0689,0x01fc, 0x068e,0x0162, 0x068e,0x0183, 
    0x0691,0x0200, 0x0696,0x0023, 0x0696,0x00e0, 0x0696,0x00fb, 0x0696,0x0268, 
    0x069a,0x0282, 0x069d,0x007e, 0x06a2,0x004b, 0x06a2,0x023e, 0x06a2,0x02dc, 
    0x06a6,0x0097, 0x06aa,0x02b1, 0x06aa,0x02ce, 0x06ae,0x0039, 0x06ae,0x0213, 
    0x06ae,0x022e, 0x06ae,0x02e1, 0x06b2,0x0162, 0x06b2,0x0183, 0x06b6,0x0023, 
    0x06b6,0x00e0, 0x06b6,0x00fb, 0x06ba,0x008f, 0x06ba,0x01e4, 0x06be,0x034b, 
    0x06c1,0x0086, 0x06c1,0x009a, 0x06c1,0x00ec, 0x06c1,0x0172, 0x06c6,0x01da, 
    0x06c6,0x0280, 0x06c6,0x0351, 0x06ce,0x008f, 0x06d2,0x01e3, 0x06d2,0x0287, 
    0x06d2,0x0353, 0x06d6,0x027a, 0x06d6,0x029b, 0x06da,0x0033, 0x06da,0x01ce, 
    0x06da,0x01f1, 0x06de,0x0133, 0x06de,0x0148, 0x06e2,0x0021, 0x06e2,0x00bf, 
    0x06e2,0x00d0, 0x06e5,0x023a, 0x06e9,0x0004, 0x06ee,0x028c, 0x06ee,0x0338, 
    0x06f2,0x0328, 0x06f2,0x0330, 0x06f4,0x0005, 0x06f9,0x01e0, 0x06fe,0x0328, 
    0x06fe,0x0330, 0x0702,0x003d, 0x0702,0x00f3, 0x0702,0x0330, 0x0704,0x0003, 
    0x070a,0x003d, 0x070a,0x00f3, 0x070a,0x01d4, 0x070a,0x01f3, 0x070a,0x0330, 
    0x0711,0x032a, 0x0711,0x032e, 0x0716,0x003d, 0x0718,0x0205, 0x0718,0x0282, 
    0x071e,0x00f3, 0x0720,0x01dc, 0x0720,0x02a5, 0x0726,0x0324, 0x072a,0x028a, 
    0x072a,0x02a7, 0x0729,0x031c, 0x0729,0x032a, 0x072e,0x003d, 0x072e,0x00f9, 
    0x072e,0x022d, 0x072e,0x0248, 0x072e,0x02e4, 0x0730,0x003d, 0x0730,0x0247, 
    0x0730,0x02e3, 0x0730,0x0324, 0x0732,0x0324, 0x0739,0x032e, 0x073e,0x003d, 
    0x0740,0x003d, 0x0744,0x027a, 0x0744,0x029b, 0x0748,0x0033, 0x0748,0x01ce, 
    0x0748,0x01f1, 0x074c,0x0162, 0x074c,0x0183, 0x0750,0x0023, 0x0750,0x00e0, 
    0x0750,0x00fb, 0x0755,0x0246, 0x075a,0x0095, 0x075a,0x0397, 0x075d,0x0004, 
    0x076a,0x03b3, 0x076d,0x0002, 0x0772,0x02fb, 0x0772,0x0301, 0x0772,0x0315, 
    0x0772,0x0397, 0x0776,0x008f, 0x077e,0x0027, 0x078a,0x00a1, 0x0792,0x009d, 
    0x0792,0x00c3, 0x0792,0x02fb, 0x0792,0x0301, 0x0792,0x0315, 0x0792,0x03bd, 
    0x0796,0x0027, 0x0796,0x024f, 0x079e,0x009d, 0x07a6,0x009d, 0x07a6,0x02fb, 
    0x07a6,0x0301, 0x07a6,0x0315, 0x07a6,0x03bd, 0x07aa,0x0027, 0x07aa,0x024f, 
    0x07ae,0x009d, 0x07b9,0x004e, 0x07b8,0x0087, 0x07b8,0x00ed, 0x07b8,0x0173, 
    0x07b8,0x0197, 0x07b9,0x021a, 0x07b9,0x02b8, 0x07b9,0x0364, 0x07be,0x0029, 
    0x07be,0x0030, 0x07c0,0x017b, 0x07c6,0x017b, 0x07c8,0x00f3, 0x07ce,0x00f3, 
    0x07d0,0x008f, 0x07d6,0x008f, 0x07d9,0x01e8, 0x07dd,0x0292, 0x07e2,0x0053, 
    0x07e6,0x008f, 0x07e6,0x00f3, 0x07e6,0x017b, 0x07e8,0x0029, 0x07e8,0x0030, 
    0x07ec,0x0021, 0x07ec,0x02ad, 0x07f2,0x0181, 0x07f2,0x0315, 0x07f4,0x0021, 
    0x07f8,0x020f, 0x07fd,0x002e, 0x0800,0x008f, 0x0805,0x0006, 0x0809,0x03c2, 
    0x080d,0x0084, 0x0812,0x0009, 0x0811,0x0008, 0x0812,0x00f3, 0x0812,0x00f9, 
    0x0812,0x017b, 0x0812,0x0181, 0x0814,0x0033, 0x0818,0x0023, 0x081c,0x0285, 
    0x0826,0x03bd, 0x082c,0x008f, 0x082c,0x017b, 0x0832,0x0043, 0x0832,0x011b, 
    0x0832,0x01b3, 0x0832,0x01c3, 0x0835,0x032a, 0x0838,0x0085, 0x0839,0x032a, 
    0x083e,0x0049, 0x083d,0x0084, 0x083e,0x02fb, 0x083e,0x0301, 0x083e,0x0315, 
    0x083e,0x0397, 0x0842,0x0009, 0x0841,0x0008, 0x0844,0x0009, 0x0846,0x008f, 
    0x084a,0x0033, 0x084e,0x0285, 0x0851,0x009a, 0x0856,0x00a1, 0x0859,0x031c, 
    0x085d,0x00b2, 0x0861,0x0012, 0x0861,0x02cc, 0x0865,0x0058, 0x0865,0x007e, 
    0x0869,0x004a, 0x0871,0x0010, 0x0876,0x003d, 0x0879,0x032c, 0x087e,0x0089, 
    0x0882,0x0229, 0x0882,0x022d, 0x0882,0x02c7, 0x0882,0x02cb, 0x0886,0x0021, 
    0x0886,0x02ad, 0x0885,0x0356, 0x088a,0x0017, 0x088a,0x020f, 0x0889,0x0354, 
    0x088d,0x009c, 0x0892,0x0089, 0x0895,0x0246, 0x089a,0x03bd, 0x089e,0x008f, 
    0x089e,0x02f9, 0x089e,0x0313, 0x08a1,0x032a, 0x08a6,0x0053, 0x08a6,0x0095, 
    0x08a6,0x0397, 0x08a8,0x017b, 0x08ad,0x031a, 0x08b2,0x017b, 0x08b4,0x00f3, 
    0x08b5,0x02a0, 0x08b8,0x0089, 0x08c1,0x0024, 0x08c4,0x00f3, 0x08c9,0x007e, 
    0x08cd,0x007c, 0x08cd,0x0222, 0x08cd,0x0294, 0x08d1,0x003a, 0x08d6,0x0009, 
    0x08d9,0x003a, 0x08dc,0x001f, 0x08e0,0x008f, 0x08e0,0x017b, 0x08e4,0x0009, 
    0x08e8,0x01ed, 0x08ed,0x031c, 0x08f2,0x003d, 0x08f6,0x008f, 0x08f6,0x017b, 
    0x08fa,0x0009, 0x08fe,0x003d, 0x0902,0x01e9, 0x0904,0x01e9, 0x0904,0x0381, 
    0x090a,0x03b1, 0x090d,0x031a, 0x0910,0x0299, 0x0914,0x034b, 0x0919,0x0008, 
    0x091c,0x0033, 0x091c,0x003d, 0x0920,0x0027, 0x0924,0x0027, 0x0924,0x01fb, 
    0x092a,0x01ce, 0x092a,0x01f1, 0x092d,0x031c, 0x0930,0x001f, 0x0936,0x00c5, 
    0x0938,0x00c5, 0x0938,0x0381, 0x093c,0x001b, 0x0942,0x017d, 0x094a,0x0027, 
    0x094e,0x0027, 0x094e,0x01fb, 0x0952,0x03b1, 0x095a,0x0029, 0x095a,0x0030, 
    0x095d,0x0030, 0x0961,0x0030, 0x0966,0x02f9, 0x0966,0x0313, 0x0968,0x02eb, 
    0x096d,0x0008, 0x0970,0x017b, 0x0974,0x0033, 0x0979,0x0150, 0x097d,0x009a, 
    0x0982,0x0293, 0x0984,0x0293, 0x0984,0x0379, 0x098a,0x02eb, 0x098e,0x0009, 
    0x0992,0x003d, 0x0996,0x003d, 0x0999,0x0062, 0x099e,0x003d, 0x09a0,0x0027, 
    0x09a5,0x0144, 0x09a8,0x02b5, 0x09ae,0x008f, 0x09ae,0x009d, 0x09b2,0x004d, 
    0x09b2,0x0053, 0x09b2,0x00c3, 0x09b2,0x013d, 0x09b2,0x01c5, 0x09b2,0x0271, 
    0x09b4,0x0025, 0x09ba,0x0033, 0x09ba,0x0079, 0x09bc,0x0015, 0x09c2,0x013f, 
    0x09c4,0x013f, 0x09c4,0x0379, 0x09ca,0x02b5, 0x09cd,0x0006, 0x09da,0x0009, 
    0x09d9,0x0008, 0x09dc,0x000b, 0x09dc,0x004f, 0x09dd,0x0086, 0x09e0,0x0009, 
    0x09e6,0x00a1, 0x09e8,0x0009, 0x09ed,0x0086, 0x09f2,0x001f, 0x09f2,0x002f, 
    0x09f2,0x0049, 0x09f2,0x006f, 0x09f2,0x0085, 0x09f2,0x0091, 0x09f2,0x00a9, 
    0x09f2,0x00d3, 0x09f2,0x00d7, 0x09f2,0x011d, 0x09f2,0x0121, 0x09f2,0x0235, 
    0x09f2,0x0393, 0x09f6,0x0324, 0x09f8,0x0049, 0x09f8,0x00a9, 0x09f8,0x011d, 
    0x09fe,0x001f, 0x09fe,0x0029, 0x09fe,0x0033, 0x09fe,0x003d, 0x09fe,0x0085, 
    0x09fe,0x008f, 0x09fe,0x00d3, 0x0a00,0x003d, 0x0a06,0x012d, 0x0a0e,0x00b3, 
    0x0a10,0x000b, 0x0a10,0x0387, 0x0a16,0x0059, 0x0a18,0x0009, 0x0a1e,0x0043, 
    0x0a24,0x0085, 0x0a2a,0x0009, 0x0a2d,0x0008, 0x0a32,0x028a, 0x0a32,0x02a7, 
    0x0a31,0x031c, 0x0a35,0x032e, 0x0a39,0x0006, 0x0a3a,0x0105, 0x0a3a,0x024f, 
    0x0a3c,0x0299, 0x0a42,0x01ed, 0x0a46,0x0299, 0x0a48,0x01ed, 0x0a4c,0x0059, 
    0x0a52,0x000b, 0x0a52,0x0387, 0x0a56,0x000b, 0x0a5e,0x0009, 0x0a60,0x003d, 
    0x0a66,0x0105, 0x0a6a,0x0195, 0x0a6c,0x000b, 0x0a76,0x0053, 0x0a78,0x0009, 
    0x0a7a,0x008f, 0x0a82,0x0299, 0x0a86,0x01ed, 0x0a8a,0x0027, 0x0a8e,0x004b, 
    0x0a92,0x003d, 0x0a95,0x0322, 0x0a99,0x0038, 0x0a99,0x0090, 0x0a9c,0x0061, 
    0x0a9c,0x00c7, 0x0a9c,0x012d, 0x0a9c,0x016f, 0x0a9c,0x017d, 0x0a9c,0x02c9, 
    0x0a9c,0x0383, 0x0aa1,0x0010, 0x0aa4,0x00b3, 0x0aa8,0x002f, 0x0aac,0x0027, 
    0x0ab0,0x004b, 0x0ab4,0x0043, 0x0ab9,0x0090, 0x0abd,0x0010, 0x0ac4,0x0019, 
    0x0acc,0x00f5, 0x0acc,0x022b, 0x0acc,0x037b, 0x0ad2,0x008f, 0x0ad2,0x01f1, 
    0x0ad6,0x0324, 0x0ad9,0x0330, 0x0ade,0x008f, 0x0ade,0x01f1, 0x0ae0,0x017b, 
    0x0ae4,0x008f, 0x0ae9,0x004e, 0x0aee,0x0027, 0x0af2,0x028a, 0x0af2,0x02a7, 
    0x0af1,0x031c, 0x0af6,0x0027, 0x0af9,0x031c, 0x0afe,0x00e9, 0x0afe,0x02bb, 
    0x0b02,0x000b, 0x0b06,0x00f5, 0x0b06,0x022b, 0x0b06,0x037b, 0x0b0a,0x003d, 
    0x0000,0x0000 
};


