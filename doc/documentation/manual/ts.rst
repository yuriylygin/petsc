.. _chapter_ts:

TS: Scalable ODE and DAE Solvers
--------------------------------

The ``TS`` library provides a framework for the scalable solution of
ODEs and DAEs arising from the discretization of time-dependent PDEs.

**Simple Example:** Consider the PDE

.. math:: u_t = u_{xx}

discretized with centered finite differences in space yielding the
semi-discrete equation

.. math::

   \begin{aligned}
             (u_i)_t & =  & \frac{u_{i+1} - 2 u_{i} + u_{i-1}}{h^2}, \\
              u_t      &  = & \tilde{A} u;\end{aligned}

or with piecewise linear finite elements approximation in space
:math:`u(x,t) \doteq \sum_i \xi_i(t) \phi_i(x)` yielding the
semi-discrete equation

.. math:: B {\xi}'(t) = A \xi(t)

Now applying the backward Euler method results in

.. math:: ( B - dt^n A  ) u^{n+1} = B u^n,

in which

.. math:: {u^n}_i = \xi_i(t_n) \doteq u(x_i,t_n),

.. math:: {\xi}'(t_{n+1}) \doteq \frac{{u^{n+1}}_i - {u^{n}}_i }{dt^{n}},

:math:`A` is the stiffness matrix, and :math:`B` is the identity for
finite differences or the mass matrix for the finite element method.

The PETSc interface for solving time dependent problems assumes the
problem is written in the form

.. math:: F(t,u,\dot{u}) = G(t,u), \quad u(t_0) = u_0.

In general, this is a differential algebraic equation (DAE)  [4]_. For
ODE with nontrivial mass matrices such as arise in FEM, the implicit/DAE
interface significantly reduces overhead to prepare the system for
algebraic solvers (``SNES``/``KSP``) by having the user assemble the
correctly shifted matrix. Therefore this interface is also useful for
ODE systems.

To solve an ODE or DAE one uses:

-  Function :math:`F(t,u,\dot{u})`

   ::

      TSSetIFunction(TS ts,Vec R,PetscErrorCode (*f)(TS,PetscReal,Vec,Vec,Vec,void*),void *funP);

   The vector ``R`` is an optional location to store the residual. The
   arguments to the function ``f()`` are the timestep context, current
   time, input state :math:`u`, input time derivative :math:`\dot{u}`,
   and the (optional) user-provided context ``funP``. If
   :math:`F(t,u,\dot{u}) = \dot{u}` then one need not call this
   function.

-  Function :math:`G(t,u)`, if it is nonzero, is provided with the
   function

   ::

      TSSetRHSFunction(TS ts,Vec R,PetscErrorCode (*f)(TS,PetscReal,Vec,Vec,void*),void *funP);

-  | Jacobian
     :math:`\sigma F_{\dot{u}}(t^n,u^n,\dot{u}^n) + F_u(t^n,u^n,\dot{u}^n)`
   | If using a fully implicit or semi-implicit (IMEX) method one also
     can provide an appropriate (approximate) Jacobian matrix of
     :math:`F()`.

   ::

      TSSetIJacobian(TS ts,Mat A,Mat B,PetscErrorCode (*fjac)(TS,PetscReal,Vec,Vec,PetscReal,Mat,Mat,void*),void *jacP);

   The arguments for the function ``fjac()`` are the timestep context,
   current time, input state :math:`u`, input derivative
   :math:`\dot{u}`, input shift :math:`\sigma`, matrix :math:`A`,
   preconditioning matrix :math:`B`, and the (optional) user-provided
   context ``jacP``.

   The Jacobian needed for the nonlinear system is, by the chain rule,

   .. math::

      \begin{aligned}
          \frac{d F}{d u^n} &  = &  \frac{\partial F}{\partial \dot{u}}|_{u^n} \frac{\partial \dot{u}}{\partial u}|_{u^n} + \frac{\partial F}{\partial u}|_{u^n}.\end{aligned}

   For any ODE integration method the approximation of :math:`\dot{u}`
   is linear in :math:`u^n` hence
   :math:`\frac{\partial \dot{u}}{\partial u}|_{u^n} = \sigma`, where
   the shift :math:`\sigma` depends on the ODE integrator and time step
   but not on the function being integrated. Thus

   .. math::

      \begin{aligned}
          \frac{d F}{d u^n} &  = &    \sigma F_{\dot{u}}(t^n,u^n,\dot{u}^n) + F_u(t^n,u^n,\dot{u}^n).\end{aligned}

   This explains why the user provide Jacobian is in the given form for
   all integration methods. An equivalent way to derive the formula is
   to note that

   .. math:: F(t^n,u^n,\dot{u}^n) = F(t^n,u^n,w+\sigma*u^n)

   where :math:`w` is some linear combination of previous time solutions
   of :math:`u` so that

   .. math:: \frac{d F}{d u^n} = \sigma F_{\dot{u}}(t^n,u^n,\dot{u}^n) + F_u(t^n,u^n,\dot{u}^n)

   again by the chain rule.

   For example, consider backward Euler’s method applied to the ODE
   :math:`F(t, u, \dot{u}) = \dot{u} - f(t, u)` with
   :math:`\dot{u} = (u^n - u^{n-1})/\delta t` and
   :math:`\frac{\partial \dot{u}}{\partial u}|_{u^n} = 1/\delta t`
   resulting in

   .. math::

      \begin{aligned}
          \frac{d F}{d u^n} & = &   (1/\delta t)F_{\dot{u}} + F_u(t^n,u^n,\dot{u}^n).\end{aligned}

   But :math:`F_{\dot{u}} = 1`, in this special case, resulting in the
   expected Jacobian :math:`I/\delta t - f_u(t,u^n)`.

-  | Jacobian :math:`G_u`
   | If using a fully implicit method and the function :math:`G()` is
     provided, one also can provide an appropriate (approximate)
     Jacobian matrix of :math:`G()`.

   ::

      TSSetRHSJacobian(TS ts,Mat A,Mat B,
      PetscErrorCode (*fjac)(TS,PetscReal,Vec,Mat,Mat,void*),void *jacP);

   The arguments for the function ``fjac()`` are the timestep context,
   current time, input state :math:`u`, matrix :math:`A`,
   preconditioning matrix :math:`B`, and the (optional) user-provided
   context ``jacP``.

Providing appropriate :math:`F()` and :math:`G()` for your problem
allows for the easy runtime switching between explicit, semi-implicit
(IMEX), and fully implicit methods.

Basic TS Options
~~~~~~~~~~~~~~~~

The user first creates a ``TS`` object with the command

::

   int TSCreate(MPI_Comm comm,TS *ts);

::

   int TSSetProblemType(TS ts,TSProblemType problemtype);

The ``TSProblemType`` is one of ``TS_LINEAR`` or ``TS_NONLINEAR``.

To set up ``TS`` for solving an ODE, one must set the “initial
conditions” for the ODE with

::

   TSSetSolution(TS ts, Vec initialsolution);

One can set the solution method with the routine

::

   TSSetType(TS ts,TSType type);

| Currently supported types are ``TSEULER``, ``TSRK`` (Runge-Kutta),
  ``TSBEULER``, ``TSCN`` (Crank-Nicolson), ``TSTHETA``, ``TSGLLE``
  (generalized linear), ``TSPSEUDO``, and ``TSSUNDIALS`` (only if the
  Sundials package is installed), or the command line option
| ``-ts_type euler,rk,beuler,cn,theta,gl,pseudo,sundials,eimex,arkimex,rosw``.

A list of available methods is given in the following table.

.. list-table:: Time integration schemes
   :name: tab_TSPET
   :header-rows: 1

   * - TS Name
     - Reference
     - Class
     - Type
     - Order
   * - euler
     - forward Euler
     - one-step
     - explicit
     - :math:`1`
   * - ssp
     - multistage SSP :cite:`Ketcheson_2008`
     - Runge-Kutta
     - explicit
     - :math:`\le 4`
   * - rk*
     - multiscale
     - Runge-Kutta
     - explicit
     - :math:`\ge 1`
   * - beuler
     - backward Euler
     - one-step
     - implicit
     - :math:`1`
   * - cn
     - Crank-Nicolson
     - one-step
     - implicit
     - :math:`2`
   * - theta*
     - theta-method
     - one-step
     - implicit
     - :math:`\le 2`
   * - alpha
     - alpha-method :cite:`Jansen_2000`
     - one-step
     - implicit
     - :math:`2`
   * - gl
     - general linear :cite:`Butcher_2007`
     - multistep-multistage
     - implicit
     - :math:`\le 3`
   * - eimex
     - extrapolated IMEX :cite:`Constantinescu_A2010a`
     - one-step
     - :math:`\ge 1`, adaptive
     -
   * - arkimex
     - See :any:`tab_IMEX_RK_PETSc`
     - IMEX Runge-Kutta
     - IMEX
     - :math:`1-5`
   * - rosw
     - See :any:`tab_IMEX_RosW_PETSc`
     - Rosenbrock-W
     - linearly implicit
     - :math:`1-4`
   * - glee
     - See :any:`tab_IMEX_GLEE_PETSc`
     - GL with global error
     - explicit and implicit
     - :math:`1-3`

Set the initial time with the command

::

   TSSetTime(TS ts,PetscReal time);

One can change the timestep with the command

::

   TSSetTimeStep(TS ts,PetscReal dt);

can determine the current timestep with the routine

::

   TSGetTimeStep(TS ts,PetscReal* dt);

Here, “current” refers to the timestep being used to attempt to promote
the solution form :math:`u^n` to :math:`u^{n+1}.`

One sets the total number of timesteps to run or the total time to run
(whatever is first) with the commands

::

   TSSetMaxSteps(TS ts,PetscInt maxsteps);
   TSSetMaxTime(TS ts,PetscReal maxtime);

and determines the behavior near the final time with

::

   TSSetExactFinalTime(TS ts,TSExactFinalTimeOption eftopt);

where ``eftopt`` is one of
``TS_EXACTFINALTIME_STEPOVER``,\ ``TS_EXACTFINALTIME_INTERPOLATE``, or
``TS_EXACTFINALTIME_MATCHSTEP``. One performs the requested number of
time steps with

::

   TSSolve(TS ts,Vec U);

The solve call implicitly sets up the timestep context; this can be done
explicitly with

::

   TSSetUp(TS ts);

One destroys the context with

::

   TSDestroy(TS *ts);

and views it with

::

   TSView(TS ts,PetscViewer viewer);

In place of ``TSSolve()``, a single step can be taken using

::

   TSStep(TS ts);

.. _sec_imex:

DAE Formulations
~~~~~~~~~~~~~~~~

You can find a discussion of DAEs in :cite:`AscherPetzold1998` or `Scholarpedia <http://www.scholarpedia.org/article/Differential-algebraic_equations>`__. In PETSc, TS deals with the semi-discrete form of the equations, so that space has already been discretized. If the DAE depends explicitly on the coordinate :math:`x`, then this will just appear as any other data for the equation, not as an explicit argument. Thus we have

.. math::

  F(t, u, \dot{u}) = 0

In this form, only fully implicit solvers are appropriate. However, specialized solvers for restricted forms of DAE are supported by PETSc. Below we consider an ODE which is augmented with algebraic constraints on the variables.

Hessenberg Index-1 DAE
``````````````````````

  This is a Semi-Explicit Index-1 DAE which has the form

.. math::

  \begin{aligned}
    \dot{u} &= f(t, u, z) \\
          0 &= h(t, u, z)
  \end{aligned}

where :math:`z` is a new constraint variable, and the Jacobian :math:`\frac{dh}{dz}` is non-singular everywhere. We have suppressed the :math:`x` dependence since it plays no role here. Using the non-singularity of the Jacobian and the Implicit Function Theorem, we can solve for :math:`z` in terms of :math:`u`. This means we could, in principle, plug :math:`z(u)` into the first equation to obtain a simple ODE, even if this is not the numerical process we use. Below we show that this type of DAE can be used with IMEX schemes.

Hessenberg Index-2 DAE
``````````````````````

  This DAE has the form

.. math::

  \begin{aligned}
    \dot{u} &= f(t, u, z) \\
          0 &= h(t, u)
  \end{aligned}

Notice that the constraint equation :math:`h` is not a function of the constraint variable :math:'z'. This means that we cannot naively invert as we did in the index-1 case. Our strategy will be to convert this into an index-1 DAE using a time derivative, which loosely corresponds to the idea of index being the number of derivatives necessary to get back to an ODE. If we differentiate the constraint equation with respect to time, we can use the ODE to simplify it,

.. math::

  \begin{aligned}
          0 &= \dot{h}(t, u) \\
            &= \frac{dh}{du} \dot{u} + \frac{\partial h}{\partial t} \\
            &= \frac{dh}{du} f(t, u, z) + \frac{\partial h}{\partial t}
  \end{aligned}

If the Jacobian :math:`\frac{dh}{du} \frac{df}{dz}` is non-singular, then we have precisely a semi-explicit index-1 DAE, and we can once again use the PETSc IMEX tools to solve it. A common example of an index-2 DAE is the incompressible Navier-Stokes equations, since the continuity equation :math:`\nabla\cdot u = 0` does not involve the pressure. Using PETSc IMEX with the above conversion then corresponds to the Segregated Runge-Kutta method applied to this equation :cite:`ColomesBadia2016`.

Using Implicit-Explicit (IMEX) Methods
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For “stiff” problems or those with multiple time scales :math:`F()` will
be treated implicitly using a method suitable for stiff problems and
:math:`G()` will be treated explicitly when using an IMEX method like
TSARKIMEX. :math:`F()` is typically linear or weakly nonlinear while
:math:`G()` may have very strong nonlinearities such as arise in
non-oscillatory methods for hyperbolic PDE. The user provides three
pieces of information, the APIs for which have been described above.

-  “Slow” part :math:`G(t,u)` using ``TSSetRHSFunction()``.

-  “Stiff” part :math:`F(t,u,\dot u)` using ``TSSetIFunction()``.

-  Jacobian :math:`F_u + \sigma F_{\dot u}` using ``TSSetIJacobian()``.

The user needs to set ``TSSetEquationType()`` to ``TS_EQ_IMPLICIT`` or
higher if the problem is implicit; e.g.,
:math:`F(t,u,\dot u) = M \dot u - f(t,u)`, where :math:`M` is not the
identity matrix:

-  the problem is an implicit ODE (defined implicitly through
   ``TSSetIFunction()``) or

-  a DAE is being solved.

An IMEX problem representation can be made implicit by setting ``TSARKIMEXSetFullyImplicit()``.

In PETSc, DAEs and ODEs are formulated as :math:`F(t,u,\dot{u})=G(t,u)`, where :math:`F()` is meant to be integrated implicitly and :math:`G()` explicitly. An IMEX formulation such as :math:`M\dot{u}=f(t,u)+g(t,u)` requires the user to provide :math:`M^{-1} g(t,u)` or solve :math:`g(t,u) - M x=0` in place of :math:`G(t,u)`. General cases such as :math:`F(t,u,\dot{u})=G(t,u)` are not amenable to IMEX Runge-Kutta, but can be solved by using fully implicit methods. Some use-case examples for ``TSARKIMEX`` are listed in :numref:`tab_DE_forms` and a list of methods with a summary of their properties is given in :any:`tab_IMEX_RK_PETSc`.

.. list-table:: Use case examples for ``TSARKIMEX``
   :name: tab_DE_forms
   :widths: 40 40 80

   * - :math:`\dot{u} = g(t,u)`
     - nonstiff ODE
     - :math:`\begin{aligned}F(t,u,\dot{u}) &= \dot{u} \\ G(t,u) &= g(t,u)\end{aligned}`
   * - :math:`M \dot{u} = g(t,u)`
     - nonstiff ODE with mass matrix
     - :math:`\begin{aligned}F(t,u,\dot{u}) &= \dot{u} \\ G(t,u) &= M^{-1} g(t,u)\end{aligned}`
   * - :math:`\dot{u} = f(t,u)`
     - stiff ODE
     - :math:`\begin{aligned}F(t,u,\dot{u}) &= \dot{u} - f(t,u) \\ G(t,u) &= 0\end{aligned}`
   * - :math:`M \dot{u} = f(t,u)`
     - stiff ODE with mass matrix
     - :math:`\begin{aligned}F(t,u,\dot{u}) &= M \dot{u} - f(t,u) \\ G(t,u) &= 0\end{aligned}`
   * - :math:`\dot{u} = f(t,u) + g(t,u)`
     - stiff-nonstiff ODE
     - :math:`\begin{aligned}F(t,u,\dot{u}) &= \dot{u} - f(t,u) \\ G(t,u) &= g(t,u)\end{aligned}`
   * - :math:`M \dot{u} = f(t,u) + g(t,u)`
     - stiff-nonstiff ODE with mass matrix
     - :math:`\begin{aligned}F(t,u,\dot{u}) &= M\dot{u} - f(t,u) \\ G(t,u) &= M^{-1} g(t,u)\end{aligned}`
   * - :math:`\begin{aligned}\dot{u} &= f(t,u,z) + g(t,u,z)\\0 &= h(t,y,z)\end{aligned}`
     - semi-explicit index-1 DAE
     - :math:`\begin{aligned}F(t,u,\dot{u}) &= \begin{pmatrix}\dot{u} - f(t,u,z)\\h(t, u, z)\end{pmatrix}\\G(t,u) &= g(t,u)\end{aligned}`
   * - :math:`f(t,u,\dot{u})=0`
     - fully implicit ODE/DAE
     - :math:`\begin{aligned}F(t,u,\dot{u}) &= f(t,u,\dot{u})\\G(t,u) &= 0\end{aligned}`; the user needs to set ``TSSetEquationType()`` to ``TS_EQ_IMPLICIT`` or higher

:numref:`tab_IMEX_RK_PETSc` lists of the currently available IMEX Runge-Kutta schemes. For each method, it gives the ``-ts_arkimex_type`` name, the reference, the total number of stages/implicit stages, the order/stage-order, the implicit stability properties (IM), stiff accuracy (SA), the existence of an embedded scheme, and dense output (DO).

.. list-table:: IMEX Runge-Kutta schemes
  :name: tab_IMEX_RK_PETSc
  :header-rows: 1

  * - Name
    - Reference
    - Stages (IM)
    - Order (Stage)
    - IM
    - SA
    - Embed
    - DO
    - Remarks
  * - a2
    - based on CN
    - 2 (1)
    - 2 (2)
    - A-Stable
    - yes
    - yes (1)
    - yes (2)
    -
  * - l2
    - SSP2(2,2,2) :cite:`Pareschi_2005`
    - 2 (2)
    - 2 (1)
    - L-Stable
    - yes
    - yes (1)
    - yes (2)
    - SSP SDIRK
  * - ars122
    - ARS122 :cite:`Ascher_1997`
    - 2 (1)
    - 3 (1)
    - A-Stable
    - yes
    - yes (1)
    - yes (2)
    -
  * - 2c
    - :cite:`Giraldo_2013`
    - 3 (2)
    - 2 (2)
    - L-Stable
    - yes
    - yes (1)
    - yes (2)
    - SDIRK
  * - 2d
    - :cite:`Giraldo_2013`
    - 3 (2)
    - 2 (2)
    - L-Stable
    - yes
    - yes (1)
    - yes (2)
    - SDIRK
  * -  2e
    - :cite:`Giraldo_2013`
    - 3 (2)
    - 2 (2)
    - L-Stable
    - yes
    - yes (1)
    - yes (2)
    - SDIRK
  * - prssp2
    - PRS(3,3,2) :cite:`Pareschi_2005`
    - 3 (3)
    - 3 (1)
    - L-Stable
    - yes
    - no
    - no
    - SSP
  * - 3
    - :cite:`Kennedy_2003`
    - 4 (3)
    - 3 (2)
    - L-Stable
    - yes
    - yes (2)
    - yes (2)
    - SDIRK
  * - bpr3
    - :cite:`Boscarino_TR2011`
    - 5 (4)
    - 3 (2)
    - L-Stable
    - yes
    - no
    - no
    - SDIRK
  * - ars443
    - :cite:`Ascher_1997`
    - 5 (4)
    - 3 (1)
    - L-Stable
    - yes
    - no
    - no
    - SDIRK
  * - 4
    - :cite:`Kennedy_2003`
    - 6 (5)
    - 4 (2)
    - L-Stable
    - yes
    - yes (3)
    - yes
    - SDIRK
  * - 5
    - :cite:`Kennedy_2003`
    - 8 (7)
    - 5 (2)
    - L-Stable
    - yes
    - yes (4)
    - yes (3)
    - SDIRK

ROSW are linearized implicit Runge-Kutta methods known as Rosenbrock
W-methods. They can accommodate inexact Jacobian matrices in their
formulation. A series of methods are available in PETSc are listed in
:numref:`tab_IMEX_RosW_PETSc` below. For each method, it gives the reference, the total number of stages and implicit stages, the scheme order and stage order, the implicit stability properties (IM), stiff accuracy (SA), the existence of an embedded scheme, dense output (DO), the capacity to use inexact Jacobian matrices (-W), and high order integration of differential algebraic equations (PDAE).

.. list-table:: Rosenbrock W-schemes
   :name: tab_IMEX_RosW_PETSc
   :header-rows: 1

   * - TS
     - Reference
     - Stages (IM)
     - Order (Stage)
     - IM
     - SA
     - Embed
     - DO
     - -W
     - PDAE
     - Remarks
   * - theta1
     - classical
     - 1(1)
     - 1(1)
     - L-Stable
     - -
     - -
     - -
     - -
     - -
     - -
   * - theta2
     - classical
     - 1(1)
     - 2(2)
     - A-Stable
     - -
     - -
     - -
     - -
     - -
     - -
   * - 2m
     - Zoltan
     - 2(2)
     - 2(1)
     - L-Stable
     - No
     - Yes(1)
     - Yes(2)
     - Yes
     - No
     - SSP
   * - 2p
     - Zoltan
     - 2(2)
     - 2(1)
     - L-Stable
     - No
     - Yes(1)
     - Yes(2)
     - Yes
     - No
     - SSP
   * - ra3pw
     - :cite:`Rang_2005`
     - 3(3)
     - 3(1)
     - A-Stable
     - No
     - Yes
     - Yes(2)
     - No
     - Yes(3)
     - -
   * - ra34pw2
     - :cite:`Rang_2005`
     - 4(4)
     - 3(1)
     - L-Stable
     - Yes
     - Yes
     - Yes(3)
     - Yes
     - Yes(3)
     - -
   * - rodas3
     - :cite:`Sandu_1997`
     - 4(4)
     - 3(1)
     - L-Stable
     - Yes
     - Yes
     - No
     - No
     - Yes
     - -
   * - sandu3
     - :cite:`Sandu_1997`
     - 3(3)
     - 3(1)
     - L-Stable
     - Yes
     - Yes
     - Yes(2)
     - No
     - No
     - -
   * - assp3p3s1c
     - unpub.
     - 3(2)
     - 3(1)
     - A-Stable
     - No
     - Yes
     - Yes(2)
     - Yes
     - No
     - SSP
   * - lassp3p4s2c
     - unpub.
     - 4(3)
     - 3(1)
     - L-Stable
     - No
     - Yes
     - Yes(3)
     - Yes
     - No
     - SSP
   * - lassp3p4s2c
     - unpub.
     - 4(3)
     - 3(1)
     - L-Stable
     - No
     - Yes
     - Yes(3)
     - Yes
     - No
     - SSP
   * - ark3
     - unpub.
     - 4(3)
     - 3(1)
     - L-Stable
     - No
     - Yes
     - Yes(3)
     - Yes
     - No
     - IMEX-RK

GLEE methods
~~~~~~~~~~~~

In this section, we describe explicit and implicit time stepping methods
with global error estimation that are introduced in
:cite:`Constantinescu_TR2016b`. The solution vector for a
GLEE method is either [:math:`y`, :math:`\tilde{y}`] or
[:math:`y`,\ :math:`\varepsilon`], where :math:`y` is the solution,
:math:`\tilde{y}` is the “auxiliary solution,” and :math:`\varepsilon`
is the error. The working vector that ``TSGLEE`` uses is :math:`Y` =
[:math:`y`,\ :math:`\tilde{y}`], or [:math:`y`,\ :math:`\varepsilon`]. A
GLEE method is defined by

-  :math:`(p,r,s)`: (order, steps, and stages),

-  :math:`\gamma`: factor representing the global error ratio,

-  :math:`A, U, B, V`: method coefficients,

-  :math:`S`: starting method to compute the working vector from the
   solution (say at the beginning of time integration) so that
   :math:`Y = Sy`,

-  :math:`F`: finalizing method to compute the solution from the working
   vector,\ :math:`y = FY`.

-  :math:`F_\text{embed}`: coefficients for computing the auxiliary
   solution :math:`\tilde{y}` from the working vector
   (:math:`\tilde{y} = F_\text{embed} Y`),

-  :math:`F_\text{error}`: coefficients to compute the estimated error
   vector from the working vector
   (:math:`\varepsilon = F_\text{error} Y`).

-  :math:`S_\text{error}`: coefficients to initialize the auxiliary
   solution (:math:`\tilde{y}` or :math:`\varepsilon`) from a specified
   error vector (:math:`\varepsilon`). It is currently implemented only
   for :math:`r = 2`. We have :math:`y_\text{aux} =
   S_{error}[0]*\varepsilon + S_\text{error}[1]*y`, where
   :math:`y_\text{aux}` is the 2nd component of the working vector
   :math:`Y`.

The methods can be described in two mathematically equivalent forms:
propagate two components (“:math:`y\tilde{y}` form”) and propagating the
solution and its estimated error (“:math:`y\varepsilon` form”). The two
forms are not explicitly specified in ``TSGLEE``; rather, the specific
values of :math:`B, U, S, F, F_{embed}`, and :math:`F_{error}`
characterize whether the method is in :math:`y\tilde{y}` or
:math:`y\varepsilon` form.

The API used by this ``TS`` method includes:

-  ``TSGetSolutionComponents``: Get all the solution components of the
   working vector

   ::

          ierr = TSGetSolutionComponents(TS,int*,Vec*)

   Call with ``NULL`` as the last argument to get the total number of
   components in the working vector :math:`Y` (this is :math:`r` (not
   :math:`r-1`)), then call to get the :math:`i`-th solution component.

-  ``TSGetAuxSolution``: Returns the auxiliary solution
   :math:`\tilde{y}` (computed as :math:`F_\text{embed} Y`)

   ::

          ierr = TSGetAuxSolution(TS,Vec*)

-  ``TSGetTimeError``: Returns the estimated error vector
   :math:`\varepsilon` (computed as :math:`F_\text{error} Y` if
   :math:`n=0` or restores the error estimate at the end of the previous
   step if :math:`n=-1`)

   ::

          ierr = TSGetTimeError(TS,PetscInt n,Vec*)

-  ``TSSetTimeError``: Initializes the auxiliary solution
   (:math:`\tilde{y}` or :math:`\varepsilon`) for a specified initial
   error.

   ::

          ierr = TSSetTimeError(TS,Vec)

The local error is estimated as :math:`\varepsilon(n+1)-\varepsilon(n)`.
This is to be used in the error control. The error in :math:`y\tilde{y}`
GLEE is
:math:`\varepsilon(n) = \frac{1}{1-\gamma} * (\tilde{y}(n) - y(n))`.

Note that :math:`y` and :math:`\tilde{y}` are reported to ``TSAdapt``
``basic`` (``TSADAPTBASIC``), and thus it computes the local error as
:math:`\varepsilon_{loc} = (\tilde{y} -
y)`. However, the actual local error is :math:`\varepsilon_{loc}
= \varepsilon_{n+1} - \varepsilon_n = \frac{1}{1-\gamma} * [(\tilde{y} -
y)_{n+1} - (\tilde{y} - y)_n]`.

:numref:`tab_IMEX_GLEE_PETSc` lists currently available GL schemes with global error estimation :cite:`Constantinescu_TR2016b`.

.. list-table:: GL schemes with global error estimation
   :name: tab_IMEX_GLEE_PETSc
   :header-rows: 1

   * - TS
     - Reference
     - IM/EX
     - :math:`(p,r,s)`
     - :math:`\gamma`
     - Form
     - Notes
   * - ``TSGLEEi1``
     - ``BE1``
     - IM
     - :math:`(1,3,2)`
     - :math:`0.5`
     - :math:`y\varepsilon`
     - Based on backward Euler
   * - ``TSGLEE23``
     - ``23``
     - EX
     - :math:`(2,3,2)`
     - :math:`0`
     - :math:`y\varepsilon`
     -
   * - ``TSGLEE24``
     - ``24``
     - EX
     - :math:`(2,4,2)`
     - :math:`0`
     - :math:`y\tilde{y}`
     -
   * - ``TSGLEE25I``
     - ``25i``
     - EX
     - :math:`(2,5,2)`
     - :math:`0`
     - :math:`y\tilde{y}`
     -
   * - ``TSGLEE35``
     - ``35``
     - EX
     - :math:`(3,5,2)`
     - :math:`0`
     - :math:`y\tilde{y}`
     -
   * - ``TSGLEEEXRK2A``
     - ``exrk2a``
     - EX
     - :math:`(2,6,2)`
     - :math:`0.25`
     - :math:`y\varepsilon`
     -
   * - ``TSGLEERK32G1``
     - ``rk32g1``
     - EX
     - :math:`(3,8,2)`
     - :math:`0`
     - :math:`y\varepsilon`
     -
   * - ``TSGLEERK285EX``
     - ``rk285ex``
     - EX
     - :math:`(2,9,2)`
     - :math:`0.25`
     - :math:`y\varepsilon`
     -

Using fully implicit methods
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To use a fully implicit method like ``TSTHETA`` or ``TSGL``, either
provide the Jacobian of :math:`F()` (and :math:`G()` if :math:`G()` is
provided) or use a ``DM`` that provides a coloring so the Jacobian can
be computed efficiently via finite differences.

Using the Explicit Runge-Kutta timestepper with variable timesteps
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The explicit Euler and Runge-Kutta methods require the ODE be in the
form

.. math:: \dot{u} = G(u,t).

The user can either call ``TSSetRHSFunction()`` and/or they can call
``TSSetIFunction()`` (so long as the function provided to
``TSSetIFunction()`` is equivalent to :math:`\dot{u} + \tilde{F}(t,u)`)
but the Jacobians need not be provided. [5]_

The Explicit Runge-Kutta timestepper with variable timesteps is an
implementation of the standard Runge-Kutta with an embedded method. The
error in each timestep is calculated using the solutions from the
Runge-Kutta method and its embedded method (the 2-norm of the difference
is used). The default method is the :math:`3`\ rd-order Bogacki-Shampine
method with a :math:`2`\ nd-order embedded method (``TSRK3BS``). Other
available methods are the :math:`5`\ th-order Fehlberg RK scheme with a
:math:`4`\ th-order embedded method (``TSRK5F``), the
:math:`5`\ th-order Dormand-Prince RK scheme with a :math:`4`\ th-order
embedded method (``TSRK5DP``), the :math:`5`\ th-order Bogacki-Shampine
RK scheme with a :math:`4`\ th-order embedded method (``TSRK5BS``, and
the :math:`6`\ th-, :math:`7`\ th, and :math:`8`\ th-order robust Verner
RK schemes with a :math:`5`\ th-, :math:`6`\ th, and :math:`7`\ th-order
embedded method, respectively (``TSRK6VR``, ``TSRK7VR``, ``TSRK8VR``).
Variable timesteps cannot be used with RK schemes that do not have an
embedded method (``TSRK1FE`` - :math:`1`\ st-order, :math:`1`-stage
forward Euler, ``TSRK2A`` - :math:`2`\ nd-order, :math:`2`-stage RK
scheme, ``TSRK3`` - :math:`3`\ rd-order, :math:`3`-stage RK scheme,
``TSRK4`` - :math:`4`-th order, :math:`4`-stage RK scheme).

Special Cases
~~~~~~~~~~~~~

-  :math:`\dot{u} = A u.` First compute the matrix :math:`A` then call

   ::

      TSSetProblemType(ts,TS_LINEAR);
      TSSetRHSFunction(ts,NULL,TSComputeRHSFunctionLinear,NULL);
      TSSetRHSJacobian(ts,A,A,TSComputeRHSJacobianConstant,NULL);

   or

   ::

      TSSetProblemType(ts,TS_LINEAR);
      TSSetIFunction(ts,NULL,TSComputeIFunctionLinear,NULL);
      TSSetIJacobian(ts,A,A,TSComputeIJacobianConstant,NULL);

-  :math:`\dot{u} = A(t) u.` Use

   ::

      TSSetProblemType(ts,TS_LINEAR);
      TSSetRHSFunction(ts,NULL,TSComputeRHSFunctionLinear,NULL);
      TSSetRHSJacobian(ts,A,A,YourComputeRHSJacobian, &appctx);

   where ``YourComputeRHSJacobian()`` is a function you provide that
   computes :math:`A` as a function of time. Or use

   ::

      TSSetProblemType(ts,TS_LINEAR);
      TSSetIFunction(ts,NULL,TSComputeIFunctionLinear,NULL);
      TSSetIJacobian(ts,A,A,YourComputeIJacobian, &appctx);

Monitoring and visualizing solutions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

-  ``-ts_monitor`` - prints the time and timestep at each iteration.

-  ``-ts_adapt_monitor`` - prints information about the timestep
   adaption calculation at each iteration.

-  ``-ts_monitor_lg_timestep`` - plots the size of each timestep,
   ``TSMonitorLGTimeStep()``.

-  ``-ts_monitor_lg_solution`` - for ODEs with only a few components
   (not arising from the discretization of a PDE) plots the solution as
   a function of time, ``TSMonitorLGSolution()``.

-  ``-ts_monitor_lg_error`` - for ODEs with only a few components plots
   the error as a function of time, only if ``TSSetSolutionFunction()``
   is provided, ``TSMonitorLGError()``.

-  ``-ts_monitor_draw_solution`` - plots the solution at each iteration,
   ``TSMonitorDrawSolution()``.

-  ``-ts_monitor_draw_error`` - plots the error at each iteration only
   if ``TSSetSolutionFunction()`` is provided,
   ``TSMonitorDrawSolution()``.

-  ``-ts_monitor_solution binary[:filename]`` - saves the solution at
   each iteration to a binary file, ``TSMonitorSolution()``.

-  ``-ts_monitor_solution_vtk <filename-%03D.vts>`` - saves the solution
   at each iteration to a file in vtk format,
   ``TSMonitorSolutionVTK()``.

Error control via variable time-stepping
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Most of the time stepping methods avaialable in PETSc have an error
estimation and error control mechanism. This mechanism is implemented by
changing the step size in order to maintain user specified absolute and
relative tolerances. The PETSc object responsible with error control is
``TSAdapt``. The available ``TSAdapt`` types are listed in the following table.

.. list-table:: ``TSAdapt``: available adaptors
   :name: tab_adaptors
   :header-rows: 1

   * - ID
     - Name
     - Notes
   * - ``TSADAPTNONE``
     - ``none``
     - no adaptivity
   * - ``TSADAPTBASIC``
     - ``basic``
     - the default adaptor
   * - ``TSADAPTGLEE``
     - ``glee``
     - extension of the basic adaptor to treat :math:`{\rm Tol}_{\rm A}` and :math:`{\rm Tol}_{\rm R}` as separate criteria. It can also control global erorrs if the integrator (e.g., ``TSGLEE``) provides this information

When using ``TSADAPTBASIC`` (the default), the user typically provides a
desired absolute :math:`{\rm Tol}_{\rm A}` or a relative
:math:`{\rm Tol}_{\rm R}` error tolerance by invoking
``TSSetTolerances()`` or at the command line with options ``-ts_atol``
and ``-ts_rtol``. The error estimate is based on the local truncation
error, so for every step the algorithm verifies that the estimated local
truncation error satisfies the tolerances provided by the user and
computes a new step size to be taken. For multistage methods, the local
truncation is obtained by comparing the solution :math:`y` to a lower
order :math:`\widehat{p}=p-1` approximation, :math:`\widehat{y}`, where
:math:`p` is the order of the method and :math:`\widehat{p}` the order
of :math:`\widehat{y}`.

The adaptive controller at step :math:`n` computes a tolerance level

.. math::

   \begin{aligned}
   Tol_n(i)&=&{\rm Tol}_{\rm A}(i) +  \max(y_n(i),\widehat{y}_n(i)) {\rm Tol}_{\rm R}(i)\,,\end{aligned}

and forms the acceptable error level

.. math::

   \begin{aligned}
   \rm wlte_n&=& \frac{1}{m} \sum_{i=1}^{m}\sqrt{\frac{\left\|y_n(i)
     -\widehat{y}_n(i)\right\|}{Tol(i)}}\,,\end{aligned}

where the errors are computed componentwise, :math:`m` is the dimension
of :math:`y` and ``-ts_adapt_wnormtype`` is ``2`` (default). If
``-ts_adapt_wnormtype`` is ``infinity`` (max norm), then

.. math::

   \begin{aligned}
   \rm wlte_n&=& \max_{1\dots m}\frac{\left\|y_n(i)
     -\widehat{y}_n(i)\right\|}{Tol(i)}\,.\end{aligned}

The error tolerances are satisfied when :math:`\rm wlte\le 1.0`.

The next step size is based on this error estimate, and determined by

.. math::

   \begin{aligned}
    \Delta t_{\rm new}(t)&=&\Delta t_{\rm{old}} \min(\alpha_{\max},
    \max(\alpha_{\min}, \beta (1/\rm wlte)^\frac{1}{\widehat{p}+1}))\,,\end{aligned}

where :math:`\alpha_{\min}=`\ ``-ts_adapt_clip``\ [0] and
:math:`\alpha_{\max}`\ =\ ``-ts_adapt_clip``\ [1] keep the change in
:math:`\Delta t` to within a certain factor, and :math:`\beta<1` is
chosen through ``-ts_adapt_safety`` so that there is some margin to
which the tolerances are satisfied and so that the probability of
rejection is decreased.

This adaptive controller works in the following way. After completing
step :math:`k`, if :math:`\rm wlte_{k+1} \le 1.0`, then the step is
accepted and the next step is modified according to
(`[eq:hnew] <#eq:hnew>`__); otherwise, the step is rejected and retaken
with the step length computed in (`[eq:hnew] <#eq:hnew>`__).

``TSADAPTGLEE`` is an extension of the basic
adaptor to treat :math:`{\rm Tol}_{\rm A}` and :math:`{\rm Tol}_{\rm R}`
as separate criteria. it can also control global errors if the
integrator (e.g., ``TSGLEE``) provides this information.

Handling of discontinuities
~~~~~~~~~~~~~~~~~~~~~~~~~~~

For problems that involve discontinuous right hand sides, one can set an
“event” function :math:`g(t,u)` for PETSc to detect and locate the times
of discontinuities (zeros of :math:`g(t,u)`). Events can be defined
through the event monitoring routine

::

   TSSetEventHandler(TS ts,PetscInt nevents,PetscInt *direction,PetscBool *terminate,PetscErrorCode (*eventhandler)(TS,PetscReal,Vec,PetscScalar*,void* eventP),PetscErrorCode (*postevent)(TS,PetscInt,PetscInt[],PetscReal,Vec,PetscBool,void* eventP),void *eventP);

Here, ``nevents`` denotes the number of events, ``direction`` sets the
type of zero crossing to be detected for an event (+1 for positive
zero-crossing, -1 for negative zero-crossing, and 0 for both),
``terminate`` conveys whether the time-stepping should continue or halt
when an event is located, ``eventmonitor`` is a user- defined routine
that specifies the event description, ``postevent`` is an optional
user-defined routine to take specific actions following an event.

The arguments to ``eventhandler()`` are the timestep context, current
time, input state :math:`u`, array of event function value, and the
(optional) user-provided context ``eventP``.

The arguments to ``postevent()`` routine are the timestep context,
number of events occured, indices of events occured, current time, input
state :math:`u`, a boolean flag indicating forward solve (1) or adjoint
solve (0), and the (optional) user-provided context ``eventP``.

The event monitoring functionality is only available with PETSc’s
implicit time-stepping solvers ``TSTHETA``, ``TSARKIMEX``, and
``TSROSW``.

.. _sec_tchem:

Using TChem from PETSc
~~~~~~~~~~~~~~~~~~~~~~

TChem [6]_ is a package originally developed at Sandia National
Laboratory that can read in CHEMKIN [7]_ data files and compute the
right hand side function and its Jacobian for a reaction ODE system. To
utilize PETSc’s ODE solvers for these systems, first install PETSc with
the additional ``configure`` option ``--download-tchem``. We currently
provide two examples of its use; one for single cell reaction and one
for an “artificial” one dimensional problem with periodic boundary
conditions and diffusion of all species. The self-explanatory examples
are the
`The TS tutorial extchem <../../src/ts/tutorials/extchem.c.html>`__
and
`The TS tutorial extchemfield <../../src/ts/tutorials/extchemfield.c.html>`__.

.. _sec_sundials:

Using Sundials from PETSc
~~~~~~~~~~~~~~~~~~~~~~~~~

Sundials is a parallel ODE solver developed by Hindmarsh et al. at LLNL.
The ``TS`` library provides an interface to use the CVODE component of
Sundials directly from PETSc. (To configure PETSc to use Sundials, see
the installation guide, ``docs/installation/index.htm``.)

To use the Sundials integrators, call

::

   TSSetType(TS ts,TSType TSSUNDIALS);

or use the command line option ``-ts_type`` ``sundials``.

Sundials’ CVODE solver comes with two main integrator families, Adams
and BDF (backward differentiation formula). One can select these with

::

   TSSundialsSetType(TS ts,TSSundialsLmmType [SUNDIALS_ADAMS,SUNDIALS_BDF]);

or the command line option ``-ts_sundials_type <adams,bdf>``. BDF is the
default.

Sundials does not use the ``SNES`` library within PETSc for its
nonlinear solvers, so one cannot change the nonlinear solver options via
``SNES``. Rather, Sundials uses the preconditioners within the ``PC``
package of PETSc, which can be accessed via

::

   TSSundialsGetPC(TS ts,PC *pc);

The user can then directly set preconditioner options; alternatively,
the usual runtime options can be employed via ``-pc_xxx``.

Finally, one can set the Sundials tolerances via

::

   TSSundialsSetTolerance(TS ts,double abs,double rel);

where ``abs`` denotes the absolute tolerance and ``rel`` the relative
tolerance.

Other PETSc-Sundials options include

::

   TSSundialsSetGramSchmidtType(TS ts,TSSundialsGramSchmidtType type);

where ``type`` is either ``SUNDIALS_MODIFIED_GS`` or
``SUNDIALS_UNMODIFIED_GS``. This may be set via the options data base
with ``-ts_sundials_gramschmidt_type <modifed,unmodified>``.

The routine

::

   TSSundialsSetMaxl(TS ts,PetscInt restart);

sets the number of vectors in the Krylov subpspace used by GMRES. This
may be set in the options database with ``-ts_sundials_maxl`` ``maxl``.

.. [4]
   If the matrix :math:`F_{\dot{u}}(t) = \partial F
   / \partial \dot{u}` is nonsingular then it is an ODE and can be
   transformed to the standard explicit form, although this
   transformation may not lead to efficient algorithms.

.. [5]
   PETSc will automatically translate the function provided to the
   appropriate form.

.. [6]
   `bitbucket.org/jedbrown/tchem <https://bitbucket.org/jedbrown/tchem>`__

.. [7]
   `en.wikipedia.org/wiki/CHEMKIN <https://en.wikipedia.org/wiki/CHEMKIN>`__


.. raw:: html

    <hr>

.. bibliography:: /../src/docs/tex/petsc.bib
   :filter: docname in docnames

.. bibliography:: /../src/docs/tex/petscapp.bib
   :filter: docname in docnames
