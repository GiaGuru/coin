/* $Id: constrStandardize.cpp 1014 2013-11-12 15:22:56Z stefan $
 *
 * Name:    constrStandardize.cpp
 * Author:  Pietro Belotti
 * Purpose: standardization of constraints
 *
 * (C) Carnegie-Mellon University, 2007-11.
 * This file is licensed under the Eclipse Public License (EPL)
 */

#include "CoinHelperFunctions.hpp"

#include "BonBabSetupBase.hpp"

#include "CouenneProblemElem.hpp"
#include "CouenneProblem.hpp"

#include "CouenneExpression.hpp"
#include "CouenneExprAux.hpp"
#include "CouenneExprClone.hpp"
#include "CouenneExprIVar.hpp"
#include "CouenneDepGraph.hpp"

using namespace Couenne;

// replace a variable
void replace (CouenneProblem *p, int wind, int xind);


/// decompose body of constraint through auxiliary variables
exprAux *CouenneConstraint::standardize (CouenneProblem *p) {

  // spot an auxiliary variable in constraint's body w - f(x) and move
  // the explicit w into the vector of auxiliary variables
  //
  // do it only if this is an equality constraint and there is at
  // least one variable that did not show up so far (need a problem
  // structure)

  if (p -> Jnlst () -> ProduceOutput (Ipopt::J_ALL, J_REFORMULATE)) {
    printf ("Reformulating constraint: "); print ();
    printf (" ["); fflush (stdout); lb_ -> print ();
    printf (","); fflush (stdout);  ub_ -> print (); fflush (stdout);
    /*  printf ("] {with auxset = ");
	for (std::set <exprAux *, compExpr>::iterator i = p -> AuxSet () -> begin (); i != p -> AuxSet () -> end (); i++) {
	printf ("<"); (*i) -> print (); printf (","); (*i) -> Image () -> print (); printf ("> ");}*/
    printf ("]\n");
  }

  // Auxiliaries used to be definable with a constraint f(x) + bw = c,
  // which implies w := (c - f(x)) / b. Semi-auxiliaries no longer
  // need the equality sign, but their sign will be determined by lb_
  // and ub_. Their values allow us to decide whether we should create
  // a (semi)auxiliary or not.

  CouNumber 
    rLb = (*lb_) (),
    rUb = (*ub_) ();

  std::string 
    use_auxcons,
    use_semiaux;

  p -> bonBase () -> options () -> GetStringValue ("use_auxcons", use_auxcons, "couenne.");
  p -> bonBase () -> options () -> GetStringValue ("use_semiaux", use_semiaux, "couenne.");

  if ((  use_auxcons == "yes") &&          // if aux generated by constraints are disabled, just bail out
      (((use_semiaux == "yes") &&          // otherwise, either we allow semi-auxs
	((rLb < -COUENNE_INFINITY/2) ||    // (and either bound is infinite)
	 (rUb >  COUENNE_INFINITY/2))) ||  //
       (fabs (rLb-rUb) <= COUENNE_EPS))) { // or there is an equality constraint

    enum expression::auxSign aSign = expression::AUX_EQ;

    if      (rLb < -COUENNE_INFINITY/2) aSign = expression::AUX_LEQ;
    else if (rUb >  COUENNE_INFINITY/2) aSign = expression::AUX_GEQ;

    CouNumber rhs = rLb >= -COUENNE_INFINITY/2 ? rLb : rUb;

    // this could be the definition of a (semi)-auxiliary

    expression *rest;

    // split w from f(x)
    int wind = p -> splitAux (rhs, body_, rest, p -> Commuted (), aSign);

    //printf ("REST [%d] [%g,%g]: ", wind, p -> Lb (wind), p -> Ub (wind)); rest -> print (); printf ("\n");

    if (wind >= 0) { // this IS the definition of an auxiliary variable, w [ <= | >= | = ] f(x)

      // simplify expression (you never know)

      expression *restSimple = Simplified (rest);

      // if (restSimple) {
      // 	delete rest;
      // 	rest = restSimple;
      // }

      // if this is a constraint of the form x=k, reset x's bounds and
      // do nothing else

      if (rest -> code () == COU_EXPRCONST) {

	CouNumber constRHS = rest -> Value ();

	if (aSign != expression::AUX_LEQ) p -> Var (wind) -> lb () = constRHS;
	if (aSign != expression::AUX_GEQ) p -> Var (wind) -> ub () = constRHS;

	//delete rest;
	return NULL;
      }

      // Assign a new auxiliary variable (with the same index, "wind",
      // as the old original variable) to the expression contained in
      // "rest"

      p -> Commuted () [wind] = true;

      if (p -> Jnlst () -> ProduceOutput (Ipopt::J_ALL, J_REFORMULATE)) {
	printf ("---> %d & ", wind); fflush (stdout);
	rest -> print (); printf ("[sign: %d]\n", aSign);
      }

      assert (p -> Var (wind) -> Type () == VAR);

      int xind = rest -> Index ();

      // check if constraint now reduces to w_k = x_h, and if so
      // replace all occurrences of x_k with x_h

      if ((xind >= 0) && (aSign == expression::AUX_EQ)) {

	// create new variable, it has to be integer if original variable was integer
	exprAux *w = new exprAux (new exprClone (p -> Var (xind)), wind, 1 + p -> Var (xind) -> rank (),
				  p -> Var (wind) -> isInteger () ?
				  exprAux::Integer : exprAux::Continuous,
				  p -> domain (), aSign);
	p -> auxiliarize (w);
	w -> zeroMult ();
	
	replace (p, wind, xind);

	p -> auxiliarize (p -> Var (wind), p -> Var (xind));
	//p -> Var (wind) -> zeroMult (); // redundant variable is neutralized

      } else {

	// create new variable, it has to be integer if original variable was integer
	exprAux *w = new exprAux (rest, wind, 1 + rest -> rank (),
				  ((p -> Var (wind) -> isInteger ()) || 
				   (false && (rest -> isInteger ()) && (aSign == expression::AUX_EQ))) ? // FIXME!!!
				  exprAux::Integer : exprAux::Continuous,
				  p -> domain (), aSign);

	if (p -> Jnlst () -> ProduceOutput (Ipopt::J_ALL, J_REFORMULATE)) {
	  printf ("AuxSet:\n");
	  for (std::set <exprAux *, compExpr>::iterator i = p -> AuxSet () -> begin ();
	       i != p -> AuxSet () -> end (); ++i)
	    if ((*i) -> Image () == NULL) {
	      (*i) -> print (); printf (" does not have an image!!!\n");
	    } else {
	      printf ("-- "); (*i) -> print (); printf (" := ");
	      (*i) -> Image () -> print (); printf ("\n");
	    }
	}

	std::set <exprAux *, compExpr>::iterator i = p -> AuxSet () -> end ();

	if (aSign == expression::AUX_EQ)
	  i = p -> AuxSet () -> find (w);

	// no such expression found in the set:
	if ((i == p -> AuxSet () -> end ()) || (aSign != expression::AUX_EQ)) {

	  p -> AuxSet      () -> insert (w); // 1) beware of useless copies
	  p -> getDepGraph () -> insert (w); // 2) introduce it in acyclic structure

	  if (p -> Jnlst () -> ProduceOutput (Ipopt::J_ALL, J_REFORMULATE)) {
	    printf ("now replacing x [%d] with ", wind); fflush (stdout);
	    w -> print (); printf (" := ");
	    w -> Image () -> print (); printf ("\n");
	  }

	  // replace ALL occurrences of original variable (with index
	  // wind) with newly created auxiliary
	  p -> auxiliarize (w);
	} 

	else {

	  if (p -> Jnlst () -> ProduceOutput (Ipopt::J_ALL, J_REFORMULATE)) {
	    printf ("found aux occurrence of "); fflush (stdout);
	    w -> print (); printf (" := ");
	    w -> Image () -> print (); printf (" ... ");
	    (*i) -> print (); printf (" := ");
	    (*i) -> Image () -> print (); printf ("\n");
	  }

	  // if this is an original variable and is competing with an
	  // auxiliary variable, or at least this has a lower index,
	  // we better replace it throughout and eliminate that
	  // aux. See globallib/st_glmp_fp3 for an example where this
	  // otherwise would be a bug (x_1 unlinked from x2-x3 and
	  // leading to unbounded)
	  
	  int xind = (*i) -> Index (), iMax, iMin;

	  if (xind < wind) {
	    iMax = wind;
	    iMin = xind;
	  } else {
	    iMax = xind;
	    iMin = wind;
	  }

	  replace (p, iMax, iMin);

	  p -> auxiliarize (p -> Var (iMax), p -> Var (iMin));
	  p -> Var (iMax) -> zeroMult (); // redundant variable is neutralized
	  p -> auxiliarize (w);
	}
      }

      return NULL;
    }
  }

  if (p -> Jnlst () -> ProduceOutput (Ipopt::J_ALL, J_REFORMULATE))
    printf ("\nnormal\n-----------------\n");

  //body_ -> standardize (p, false); // TODO: check!

  //return NULL;

  return body_ -> standardize (p);
}


// Replace a variable ////////////////////////////////
void replace (CouenneProblem *p, int wind, int xind) {

  exprVar 
    *varLeaves = p -> Variables () [wind],
    *varStays  = p -> Variables () [xind];

  // intersect features of the two variables (integrality, bounds)

  varStays -> lb () = varLeaves -> lb () = CoinMax (varStays -> lb (), varLeaves -> lb ());
  varStays -> ub () = varLeaves -> ub () = CoinMin (varStays -> ub (), varLeaves -> ub ());

  if (varStays  -> isInteger () ||
      varLeaves -> isInteger ()) {

    varStays -> lb () = ceil  (varStays -> lb ());
    varStays -> ub () = floor (varStays -> ub ());

    if (varStays -> Type () == AUX)
      varStays -> setInteger (true);
    else {
      //expression *old = varStays; // !!! leak
      p -> Variables () [xind] = varStays = new exprIVar (xind, p -> domain ());
      p -> auxiliarize (varStays); // replace it everywhere in the problem
      //delete old;
    }
  }
}