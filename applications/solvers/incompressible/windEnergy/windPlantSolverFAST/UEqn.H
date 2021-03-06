    // Solve the momentum equation

    #include "computeCoriolisForce.H"

    fvVectorMatrix UEqn
    (
        fvm::ddt(U)                             // time derivative
      + fvm::div(phi, U)                        // convection
      + turbulence->divDevReff(U)               // momentum flux
      + fvc::div(Rwall)
      - fCoriolis                               // Coriolis force
      + gradPd                                  // driving pressure gradient
      - turbfast.force()                        // turbine body forces
    );

    UEqn.relax();

    if (pimple.momentumPredictor())
    {
        solve
        (
            UEqn
            ==
            fvc::reconstruct
            (
                (
                  - fvc::snGrad(p_rgh)          // modified pressure gradient
                  - ghf*fvc::snGrad(rhok)       // buoyancy force
                ) * mesh.magSf()
            )
        );
    }
