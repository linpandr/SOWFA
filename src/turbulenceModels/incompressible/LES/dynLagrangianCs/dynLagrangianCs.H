/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 2011 OpenFOAM Foundation
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

Class
    Foam::incompressible::LESModels::dynLagrangianCs

Description
    Dynamic eddy-viscosity model with Lagrangian averaging for incompressible
    flow.  Modified by Matt Churchfield to output the model constant C_s as
    a scalarField (10/15/2012).

    \verbatim
        B = 2/3*k*I - 2*nuSgs*dev(D)
        Beff = 2/3*k*I - 2*nuEff*dev(D)

    where

        D = symm(grad(U))
        nuSgs = (flm/fmm)*delta^2*sqrt(2)*|D|
        nuEff = nuSgs + nu

        Two relaxation equations are used to evaluate flm and fmm:

        d/dt(flm) + div(U*flm)
        =
        (1/T)*(L && M - flm)

        d/dt(fmm) + div(U*fmm)
        =
        (1/T)*(M && M - flm)

    where

        L = F(U.U) - F(U).F(U)
        M = 2.0 delta^2 (F(|D|.D) - 4 F(|D|).F(D))
        T = 1.5*delta*(flm.fmm)^(-1.0/8.0)

    \endverbatim

    Reference:
    \verbatim
        "A Lagrangian dynamic subgrid-scale model of turbulence"
        Charles Meneveau,
        Thomas Lund,
        William Cabot,
        J. Fluid Mech (1996), vol 319, pp. 353-385
    \endverbatim

SourceFiles
    dynLagrangianCs.C

\*---------------------------------------------------------------------------*/

#ifndef dynLagrangianCs_H
#define dynLagrangianCs_H

#include "GenEddyVisc.H"
#include "simpleFilter.H"
#include "LESfilter.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace incompressible
{
namespace LESModels
{

/*---------------------------------------------------------------------------*\
                       Class dynLagrangian Declaration
\*---------------------------------------------------------------------------*/

class dynLagrangianCs
:
    public GenEddyVisc
{
    // Private data

        volScalarField flm_;
        volScalarField fmm_;
        volScalarField Cs_;

        dimensionedScalar theta_;

        simpleFilter simpleFilter_;
        autoPtr<LESfilter> filterPtr_;
        LESfilter& filter_;

        dimensionedScalar flm0_;
        dimensionedScalar fmm0_;


    // Private Member Functions

        //- Update sub-grid scale fields
        void updateSubGridScaleFields
        (
            const tmp<volTensorField>& gradU
        );

        // Disallow default bitwise copy construct and assignment
        dynLagrangianCs(const dynLagrangianCs&);
        dynLagrangianCs& operator=(const dynLagrangianCs&);


public:

    //- Runtime type information
    TypeName("dynLagrangianCs");

    // Constructors

        //- Construct from components
        dynLagrangianCs
        (
            const volVectorField& U,
            const surfaceScalarField& phi,
            transportModel& transport,
            const word& turbulenceModelName = turbulenceModel::typeName,
            const word& modelName = typeName
        );


    //- Destructor
    virtual ~dynLagrangianCs()
    {}


    // Member Functions

        //- Return SGS kinetic energy
        tmp<volScalarField> k(const tmp<volTensorField>& gradU) const
        {
          //return 2.0*sqr(delta())*magSqr(dev(symm(gradU)));
            return pow((flm_/fmm_),2.0/3.0)*sqr(delta())*2.0*magSqr(dev(symm(gradU)))/pow(ce_,-2.0/3.0);
        }

        //- Return SGS kinetic energy
        virtual tmp<volScalarField> k() const
        {
            return k(fvc::grad(U()));
        }

        //- Return the effective diffusivity for k
        tmp<volScalarField> DkEff() const
        {
            return tmp<volScalarField>
            (
                new volScalarField("DkEff", nuSgs_ + nu())
            );
        }

        //- Correct Eddy-Viscosity and related properties
        virtual void correct(const tmp<volTensorField>& gradU);


        //- Read LESProperties dictionary
        virtual bool read();
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace LESModels
} // End namespace incompressible
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
