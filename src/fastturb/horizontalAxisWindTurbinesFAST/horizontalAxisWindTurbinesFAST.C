/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 1991-2009 OpenCFD Ltd.
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the
    Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM; if not, write to the Free Software Foundation,
    Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA

\*---------------------------------------------------------------------------*/

#include "horizontalAxisWindTurbinesFAST.H"

namespace Foam
{
namespace turbineModels
{

// * * * * * * * * * * * * * *  Constructor  * * * * * * * * * * * * * * * * //

horizontalAxisWindTurbinesFAST::horizontalAxisWindTurbinesFAST
(
    const volVectorField& U
)	      
:

    runTime_(U.time()),
	
    mesh_(U.mesh()),

    // Set the pointer to the velocity field
    U_(U),

    gradU
    (
        IOobject
        (
            "gradU",
            runTime_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedTensor("gradU",dimVelocity/dimLength,tensor::zero)
    ),

    bodyForceFAST
    (
        IOobject
        (
            "bodyForceFAST",
            runTime_.timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedVector("bodyForceFAST",dimForce/dimVolume/dimDensity,vector::zero)
    )


{

    IOdictionary turbineArrayProperties
    (
        IOobject
        (
            "turbineArrayPropertiesFAST",
            runTime_.constant(),
            mesh_,
            IOobject::MUST_READ,
            IOobject::NO_WRITE
        )
    );

    turbineName = turbineArrayProperties.toc();
    turbNum = turbineName.size() - 1;
    
    refx.setSize(turbNum);
    refy.setSize(turbNum);
    refz.setSize(turbNum);
    hubz.setSize(turbNum);

    // read turbineArray dictionary file  
    for(int i=0; i<turbNum;i++)
    {
      refx[i] = readScalar(turbineArrayProperties.subDict(turbineName[i]).lookup("refx"));
      refy[i] = readScalar(turbineArrayProperties.subDict(turbineName[i]).lookup("refy"));
      refz[i] = readScalar(turbineArrayProperties.subDict(turbineName[i]).lookup("refz"));
      hubz[i] = readScalar(turbineArrayProperties.subDict(turbineName[i]).lookup("hubz"));
    }

    yawAngle = readScalar(turbineArrayProperties.subDict(turbineName[turbNum]).lookup("yawAngle"));
    bldNum = readScalar(turbineArrayProperties.subDict(turbineName[turbNum]).lookup("numberofBld"));
    bldPts = readScalar(turbineArrayProperties.subDict(turbineName[turbNum]).lookup("numberofBldPts"));
    rotorD = readScalar(turbineArrayProperties.subDict(turbineName[turbNum]).lookup("rotorDiameter"));
    epsilon = readScalar(turbineArrayProperties.subDict(turbineName[turbNum]).lookup("epsilon"));
    smearRadius = readScalar(turbineArrayProperties.subDict(turbineName[turbNum]).lookup("smearRadius"));
    effectiveRadiusFactor = readScalar(turbineArrayProperties.subDict(turbineName[turbNum]).lookup("effectiveRadiusFactor"));
    pointInterpType = readScalar(turbineArrayProperties.subDict(turbineName[turbNum]).lookup("pointInterpType"));

    // set variable sizes
    totalBldpts = bldNum*bldPts;

    contProcNo.setSize(turbNum, List<label>(totalBldpts,0));
    minDistCellID.setSize(turbNum, List<label>(totalBldpts,-1));

    bladePoints.setSize(turbNum, List<List<vector> >(bldNum, List<vector>(bldPts,vector::zero)));
    bladeForce.setSize(turbNum, List<List<vector> >(bldNum, List<vector>(bldPts,vector::zero)));

    sphereCells.setSize(turbNum);

    yawAngle = 2.0*asin(1.0)/180.0*yawAngle;

    // initialize data-messenger variables (may not be required on some systems) 
    for(int i=0;i<turbNum;i++)
    {
      for(int j=0;j<totalBldpts;j++)
      {
        uin[i][j] = 0.0;
        vin[i][j] = 0.0;
        win[i][j] = 0.0;

        bldptx[i][j] = 0.0;
        bldpty[i][j] = 0.0;
        bldptz[i][j] = 0.0;

        bldfx[i][j] = 0.0;
        bldfy[i][j] = 0.0;
        bldfz[i][j] = 0.0;
      }
    }

    // set sphere surounding turbine
    for(int i=0;i<turbNum;i++)
    {
      getSphereCellID(i);
    }

} 


// * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * * * //
void horizontalAxisWindTurbinesFAST::getSphereCellID(int iturb) // change to sphere instead of a box 
{

    DynamicList<label> sphereCellsI;
    vector hubLoc;
    scalar effectiveRadius;

    hubLoc.x() = refx[iturb];
    hubLoc.y() = refy[iturb];
    hubLoc.z() = refz[iturb] + hubz[iturb];  
    effectiveRadius = effectiveRadiusFactor*rotorD/2.0;

    forAll(U_.mesh().cells(),cellI)
    {
      if ( mag(U_.mesh().C()[cellI] - hubLoc) <= effectiveRadius ) 
      {
        sphereCellsI.append(cellI);
      }
    }
    sphereCells[iturb].append(sphereCellsI);
    sphereCellsI.clear();

}


void horizontalAxisWindTurbinesFAST::getBldPos(int iturb)
{

    List<List<vector> > bldPtsLocal(Pstream::nProcs(), List<vector> (totalBldpts,vector::zero));

    for (int n=0; n<totalBldpts; n++)
    {
      bldPtsLocal[iturb][n].x() = bldptx[iturb][n]*cos(yawAngle)
                                - bldpty[iturb][n]*sin(yawAngle)
                                + refx[iturb];

      bldPtsLocal[iturb][n].y() = bldptx[iturb][n]*sin(yawAngle)
                                + bldpty[iturb][n]*cos(yawAngle)
                                + refy[iturb];

      bldPtsLocal[iturb][n].z() = bldptz[iturb][n] + refz[iturb];
    }

    Pstream::gatherList(bldPtsLocal);
    Pstream::scatterList(bldPtsLocal);

    for(int j=0;j<bldNum; j++)
    {
      for(int k=0;k<bldPts; k++)
      {

        bladePoints[iturb][j][k].x() = bldPtsLocal[iturb][k + bldPts*j].x();
        bladePoints[iturb][j][k].y() = bldPtsLocal[iturb][k + bldPts*j].y();
        bladePoints[iturb][j][k].z() = bldPtsLocal[iturb][k + bldPts*j].z();

      }
    }
 
}


void horizontalAxisWindTurbinesFAST::getContProcNoCellID(int iturb)  
{

    List<List<scalar> > minDisLocal(Pstream::nProcs(), List<scalar>(totalBldpts,1.0E30));
    List<List<label> > minDisCellIDLocal(Pstream::nProcs(), List<label>(totalBldpts,-1));

    if (sphereCells[iturb].size() > 0)
    {

      for(int j=0;j<bldNum; j++)
      {
        for(int k=0;k<bldPts; k++)
        {

          label minDisCellID = -1;
          scalar minDis = 1.0E10;
    
          forAll(sphereCells[iturb], m)
          {
            scalar dis = mag(mesh_.C()[sphereCells[iturb][m]] - bladePoints[iturb][j][k]);
            if(dis <= minDis)
            {
              minDisCellID = sphereCells[iturb][m];
              minDis = mag(mesh_.C()[minDisCellID] - bladePoints[iturb][j][k]);
            }
          }  
          minDisLocal[Pstream::myProcNo()][k+bldPts*j] = minDis;
          minDisCellIDLocal[Pstream::myProcNo()][k+bldPts*j] = minDisCellID;
        }
      }

    }

    Pstream::gatherList(minDisLocal);
    Pstream::scatterList(minDisLocal);
    Pstream::gatherList(minDisCellIDLocal);
    Pstream::scatterList(minDisCellIDLocal);

    // For each grid pts, find the control processor # and the associated cellID within the processor
    for(int n = 0; n < totalBldpts; n++)
    {
      scalar minDis = 1.0E30;
      for(int m = 0; m < Pstream::nProcs(); m++)
      {
        if(minDisLocal[m][n] <= minDis)
        {
          minDis = minDisLocal[m][n];
          contProcNo[iturb][n] = m;
          minDistCellID[iturb][n] = minDisCellIDLocal[m][n];
        }
      }
    }

}


void horizontalAxisWindTurbinesFAST::getWndVec(int iturb)
{
    gradU = fvc::grad(U_);

    getContProcNoCellID(iturb);  

    List<List<vector> > windVectorsLocal(Pstream::nProcs(), List<vector>(totalBldpts,vector::zero));

    for(int j=0;j<bldNum; j++)
    {
      for(int k=0;k<bldPts; k++)
      {
        if(Pstream::myProcNo() == contProcNo[iturb][k + bldPts*j])
        { 
          windVectorsLocal[Pstream::myProcNo()][k + bldPts*j] = U_[minDistCellID[iturb][k + bldPts*j]];

          if (pointInterpType == 1)
          {
            vector dx = bladePoints[iturb][j][k] - mesh_.C()[minDistCellID[iturb][k + bldPts*j]];
            vector dU = dx & gradU[minDistCellID[iturb][k + bldPts*j]];
            windVectorsLocal[Pstream::myProcNo()][k + bldPts*j] += dU;
          }

        }
      }
    }

    Pstream::gatherList(windVectorsLocal);
    Pstream::scatterList(windVectorsLocal);

    for(int n=0;n<totalBldpts;n++)
    {
        uin[iturb][n] = windVectorsLocal[contProcNo[iturb][n]][n].x()*cos(yawAngle)
                      + windVectorsLocal[contProcNo[iturb][n]][n].y()*sin(yawAngle);

        vin[iturb][n] = -windVectorsLocal[contProcNo[iturb][n]][n].x()*sin(yawAngle)
                      +  windVectorsLocal[contProcNo[iturb][n]][n].y()*cos(yawAngle);

        win[iturb][n] = windVectorsLocal[contProcNo[iturb][n]][n].z();
    }

}


void horizontalAxisWindTurbinesFAST::getBldPosForce(int iturb)
{

    List<List<vector> > bldPtsLocal(Pstream::nProcs(), List<vector> (totalBldpts,vector::zero));
    List<List<vector> > bldForceLocal(Pstream::nProcs(), List<vector> (totalBldpts,vector::zero));

    for (int n=0; n<totalBldpts; n++)
    {
      bldPtsLocal[iturb+5][n].x() = double(bldptx[iturb][n]);
      bldPtsLocal[iturb+5][n].y() = double(bldpty[iturb][n]);
      bldPtsLocal[iturb+5][n].z() = double(bldptz[iturb][n]);
      bldForceLocal[iturb+5][n].x() = double(bldfx[iturb][n]);
      bldForceLocal[iturb+5][n].y() = double(bldfy[iturb][n]);
      bldForceLocal[iturb+5][n].z() = double(bldfz[iturb][n]);
    }

    Pstream::gatherList(bldPtsLocal);
    Pstream::gatherList(bldForceLocal);

    Pstream::scatterList(bldPtsLocal);
    Pstream::scatterList(bldForceLocal);


    for(int j=0;j<bldNum; j++)
    {
      for(int k=0;k<bldPts; k++)
      {

        bladePoints[iturb][j][k].x() = bldPtsLocal[iturb+5][k + bldPts*j].x()*cos(yawAngle)
                                     - bldPtsLocal[iturb+5][k + bldPts*j].y()*sin(yawAngle) 
                                     + refx[iturb];

        bladePoints[iturb][j][k].y() = bldPtsLocal[iturb+5][k + bldPts*j].x()*sin(yawAngle)
                                     + bldPtsLocal[iturb+5][k + bldPts*j].y()*cos(yawAngle) 
                                     + refy[iturb];

        bladePoints[iturb][j][k].z() = bldPtsLocal[iturb+5][k + bldPts*j].z() + refz[iturb];


        bladeForce[iturb][j][k].x() = -bldForceLocal[iturb+5][k + bldPts*j].x()*cos(yawAngle)
                                      +bldForceLocal[iturb+5][k + bldPts*j].y()*sin(yawAngle);

        bladeForce[iturb][j][k].y() = -bldForceLocal[iturb+5][k + bldPts*j].x()*sin(yawAngle)
                                      -bldForceLocal[iturb+5][k + bldPts*j].y()*cos(yawAngle);

        bladeForce[iturb][j][k].z() = -bldForceLocal[iturb+5][k + bldPts*j].z();

      }
    }
 
}


void horizontalAxisWindTurbinesFAST::computeBodyForce(int iturb)
{ 
    getBldPosForce(iturb);

    if (sphereCells[iturb].size() > 0)
    {
      forAll(sphereCells[iturb], m)
      {
        bodyForceFAST[sphereCells[iturb][m]] = vector::zero;
        // For each blade.
        forAll(bladeForce[iturb], j)
        {
          // For each blade point.
          forAll(bladeForce[iturb][j], k)
          {
            scalar dis = mag(mesh_.C()[sphereCells[iturb][m]] - bladePoints[iturb][j][k]);
            if (dis <= smearRadius)
            {
              bodyForceFAST[sphereCells[iturb][m]] += bladeForce[iturb][j][k] * (Foam::exp(-Foam::sqr(dis/epsilon))/(Foam::pow(epsilon,3)*Foam::pow(Foam::constant::mathematical::pi,1.5)));
            }
          }
        }  
      }
    }
}

volVectorField& horizontalAxisWindTurbinesFAST::force()
{
    // Return the body force field to the solver
    return bodyForceFAST;
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace turbineModels
} // End namespace Foam

// ************************************************************************* //

