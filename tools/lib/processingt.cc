// -*- c-basic-offset: 4 -*-
/*
 * processingt.{cc,hh} -- decide on a Click configuration's processing
 * Eddie Kohler
 *
 * Copyright (c) 2000 Massachusetts Institute of Technology
 * Copyright (c) 2001 International Computer Science Institute
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include <click/config.h>

#include "processingt.hh"
#include <click/error.hh>
#include <click/bitvector.hh>
#include "elementmap.hh"
#include <ctype.h>
#include <string.h>

ProcessingT::ProcessingT()
    : _router(0)
{
}

ProcessingT::ProcessingT(const RouterT *r, ErrorHandler *errh)
    : _router(0)
{
    reset(r, errh);
}

ProcessingT::ProcessingT(const RouterT *r, ElementMap *em, ErrorHandler *errh)
    : _router(0)
{
    ElementMap::push_default(em);
    reset(r, errh);
    ElementMap::pop_default();
}

void
ProcessingT::create_pidx(ErrorHandler *errh)
{
    int ne = _router->nelements();
    _input_pidx.assign(ne, 0);
    _output_pidx.assign(ne, 0);

    // count used input and output ports for each element
    int nh = _router->nhookup();
    const Vector<Hookup> &hf = _router->hookup_from();
    const Vector<Hookup> &ht = _router->hookup_to();
    for (int i = 0; i < nh; i++) {
	const Hookup &ho = hf[i];
	if (ho.idx < 0)
	    continue;
	if (ho.port >= _output_pidx[ho.idx])
	    _output_pidx[ho.idx] = ho.port + 1;
	const Hookup &hi = ht[i];
	if (hi.port >= _input_pidx[hi.idx])
	    _input_pidx[hi.idx] = hi.port + 1;
    }

    int ci = 0, co = 0;
    for (int i = 0; i < ne; i++) {
	int ni = _input_pidx[i], no = _output_pidx[i];
	_input_pidx[i] = ci;
	_output_pidx[i] = co;
	ci += ni, co += no;
    }
    _input_pidx.push_back(ci);
    _output_pidx.push_back(co);

    // create eidxes
    _input_eidx.clear();
    _output_eidx.clear();
    ci = 0, co = 0;
    for (int i = 1; i <= ne; i++) {
	for (; ci < _input_pidx[i]; ci++)
	    _input_eidx.push_back(i - 1);
	for (; co < _output_pidx[i]; co++)
	    _output_eidx.push_back(i - 1);
    }

    // complain about dead elements with live connections
    if (errh) {
	for (int i = 0; i < ne; i++)
	    if (_router->edead(i) && (ninputs(i) > 0 || noutputs(i) > 0))
		errh->lwarning(_router->elandmark(i), "dead element %s has live connections", _router->ename(i).cc());
    }
}

static int
next_processing_code(const String &str, int &pos, ErrorHandler *errh,
		     const String &landmark, ElementClassT *etype)
{
    const char *s = str.data();
    int len = str.length();
    if (pos >= len)
	return -2;

    switch (s[pos]) {

      case 'h': case 'H':
	pos++;
	return ProcessingT::VPUSH;
    
      case 'l': case 'L':
	pos++;
	return ProcessingT::VPULL;
	
      case 'a': case 'A':
	pos++;
	return ProcessingT::VAGNOSTIC;

      case '/':
	return -2;

      default:
	errh->lerror(landmark, "bad character `%c' in processing code for `%s'", s[pos], etype->name_cc());
	pos++;
	return -1;
    
    }
}

void
ProcessingT::initial_processing_for(int ei, ErrorHandler *errh)
{
    // don't handle uprefs or tunnels
    ElementClassT *etype = _router->etype(ei);
    if (!etype || etype == ElementClassT::tunnel_type())
	return;

    String landmark = _router->elandmark(ei);
    String pc = etype->traits().processing_code();
    if (!pc) {
	errh->lwarning(landmark, "`%s' has no processing code; assuming agnostic", etype->name_cc());
	return;
    }

    int pos = 0;
    int len = pc.length();

    int start_in = _input_pidx[ei];
    int start_out = _output_pidx[ei];

    int val = 0;
    int last_val = 0;
    for (int i = 0; i < ninputs(ei); i++) {
	if (last_val >= 0)
	    last_val = next_processing_code(pc, pos, errh, landmark, etype);
	if (last_val >= 0)
	    val = last_val;
	_input_processing[start_in + i] = val;
    }

    while (pos < len && pc[pos] != '/')
	pos++;
    if (pos >= len)
	pos = 0;
    else
	pos++;

    last_val = 0;
    for (int i = 0; i < noutputs(ei); i++) {
	if (last_val >= 0)
	    last_val = next_processing_code(pc, pos, errh, landmark, etype);
	if (last_val >= 0)
	    val = last_val;
	_output_processing[start_out + i] = val;
    }
}

void
ProcessingT::initial_processing(ErrorHandler *errh)
{
    _input_processing.assign(ninput_pidx(), VAGNOSTIC);
    _output_processing.assign(noutput_pidx(), VAGNOSTIC);
    for (int i = 0; i < nelements(); i++)
	initial_processing_for(i, errh);
}

void
ProcessingT::processing_error(const Hookup &hfrom, const Hookup &hto,
			      int which, int processing_from,
			      ErrorHandler *errh)
{
  const char *type1 = (processing_from == VPUSH ? "push" : "pull");
  const char *type2 = (processing_from == VPUSH ? "pull" : "push");
  if (which < _router->nhookup())
    errh->lerror(_router->hookup_landmark(which),
		 "`%s' %s output %d connected to `%s' %s input %d",
		 _router->ename(hfrom.idx).cc(), type1, hfrom.port,
		 _router->ename(hto.idx).cc(), type2, hto.port);
  else
    errh->lerror(_router->elandmark(hfrom.idx),
		 "agnostic `%s' in mixed context: %s input %d, %s output %d",
		 _router->ename(hfrom.idx).cc(), type2, hto.port,
		 type1, hfrom.port);
}

void
ProcessingT::check_processing(ErrorHandler *errh)
{
  // add fake connections for agnostics
  Vector<Hookup> hookup_from = _router->hookup_from();
  Vector<Hookup> hookup_to = _router->hookup_to();
  Bitvector bv;
  for (int i = 0; i < ninput_pidx(); i++)
    if (_input_processing[i] == VAGNOSTIC) {
      int ei = _input_eidx[i];
      int port = i - _input_pidx[ei];
      int opidx = _output_pidx[ei];
      int noutputs = _output_pidx[ei+1] - opidx;
      forward_flow(_router->etype(ei)->traits().flow_code(),
		   port, noutputs, &bv);
      for (int j = 0; j < noutputs; j++)
	if (bv[j] && _output_processing[opidx + j] == VAGNOSTIC) {
	  hookup_from.push_back(Hookup(ei, j));
	  hookup_to.push_back(Hookup(ei, port));
	}
    }
  
  // spread personalities
  while (true) {
    
    bool changed = false;
    for (int c = 0; c < hookup_from.size(); c++) {
      if (hookup_from[c].idx < 0)
	continue;
      
      int offf = output_pidx(hookup_from[c]);
      int offt = input_pidx(hookup_to[c]);
      int pf = _output_processing[offf];
      int pt = _input_processing[offt];
      
      switch (pt) {
	
       case VAGNOSTIC:
	if (pf != VAGNOSTIC) {
	  _input_processing[offt] = pf;
	  changed = true;
	}
	break;
	
       case VPUSH:
       case VPULL:
	if (pf == VAGNOSTIC) {
	  _output_processing[offf] = pt;
	  changed = true;
	} else if (pf != pt) {
	  processing_error(hookup_from[c], hookup_to[c], c, pf, errh);
	  hookup_from[c].idx = -1;
	}
	break;
	
      }
    }
    
    if (!changed) break;
  }
}

static const char *
processing_name(int p)
{
  if (p == ProcessingT::VAGNOSTIC)
    return "agnostic";
  else if (p == ProcessingT::VPUSH)
    return "push";
  else if (p == ProcessingT::VPULL)
    return "pull";
  else
    return "?";
}

void
ProcessingT::check_connections(ErrorHandler *errh)
{
  Vector<int> input_used(ninput_pidx(), -1);
  Vector<int> output_used(noutput_pidx(), -1);
  
  // Check each hookup to ensure it doesn't reuse a port
  const Vector<Hookup> &hf = _router->hookup_from();
  const Vector<Hookup> &ht = _router->hookup_to();
  for (int c = 0; c < hf.size(); c++) {
    if (hf[c].idx < 0)
      continue;
    
    int fp = output_pidx(hf[c]), tp = input_pidx(ht[c]);

    if (_output_processing[fp] == VPUSH && output_used[fp] >= 0) {
      errh->lerror(_router->hookup_landmark(c),
		   "reuse of `%s' push output %d",
		   _router->ename(hf[c].idx).cc(), hf[c].port);
      errh->lmessage(_router->hookup_landmark(output_used[fp]),
		     "  `%s' output %d previously used here",
		     _router->ename(hf[c].idx).cc(), hf[c].port);
    } else
      output_used[fp] = c;
    
    if (_input_processing[tp] == VPULL && input_used[tp] >= 0) {
      errh->lerror(_router->hookup_landmark(c),
		   "reuse of `%s' pull input %d",
		   _router->ename(ht[c].idx).cc(), ht[c].port);
      errh->lmessage(_router->hookup_landmark(input_used[tp]),
		     "  `%s' input %d previously used here",
		     _router->ename(ht[c].idx).cc(), ht[c].port);
    } else
      input_used[tp] = c;
  }

  // Check for unused inputs and outputs, set _connected_* properly.
  for (int i = 0; i < ninput_pidx(); i++)
    if (input_used[i] < 0) {
      int ei = _input_eidx[i];
      if (_router->edead(ei)) continue;
      int port = i - _input_pidx[ei];
      errh->lerror(_router->elandmark(ei),
		   "`%s' %s input %d not connected",
		   _router->ename(ei).cc(), processing_name(_input_processing[i]), port);
    }
  
  for (int i = 0; i < noutput_pidx(); i++)
    if (output_used[i] < 0) {
      int ei = _output_eidx[i];
      if (_router->edead(ei)) continue;
      int port = i - _output_pidx[ei];
      errh->lerror(_router->elandmark(ei),
		   "`%s' %s output %d not connected",
		   _router->ename(ei).cc(), processing_name(_output_processing[i]), port);
    }

  // Set _connected_* properly.
  Hookup crap(-1, -1);
  _connected_input.assign(ninput_pidx(), crap);
  _connected_output.assign(noutput_pidx(), crap);
  for (int i = 0; i < ninput_pidx(); i++)
    if (_input_processing[i] == VPULL && input_used[i] >= 0)
      _connected_input[i] = hf[ input_used[i] ];
  for (int i = 0; i < noutput_pidx(); i++)
    if (_output_processing[i] == VPUSH && output_used[i] >= 0)
      _connected_output[i] = ht[ output_used[i] ];
}

int
ProcessingT::reset(const RouterT *r, ErrorHandler *errh)
{
  _router = r;
  if (!errh) errh = ErrorHandler::silent_handler();
  int before = errh->nerrors();

  // create pidx and eidx arrays, warn about dead elements
  create_pidx(errh);
  
  initial_processing(errh);
  check_processing(errh);
  check_connections(errh);
  if (errh->nerrors() != before)
    return -1;
  return 0;
}

bool
ProcessingT::same_processing(int a, int b) const
{
  int ai = _input_pidx[a], bi = _input_pidx[b];
  int ao = _output_pidx[a], bo = _output_pidx[b];
  int ani = _input_pidx[a+1] - ai, bni = _input_pidx[b+1] - bi;
  int ano = _output_pidx[a+1] - ao, bno = _output_pidx[b+1] - bo;
  if (ani != bni || ano != bno)
    return false;
  if (ani && memcmp(&_input_processing[ai], &_input_processing[bi], sizeof(int) * ani) != 0)
    return false;
  if (ano && memcmp(&_output_processing[ao], &_output_processing[bo], sizeof(int) * ano) != 0)
    return false;
  return true;
}

bool
ProcessingT::is_internal_flow(int, int, int) const
{
  return true;
}

// FLOW CODES

static void
skip_flow_code(const char *&p, const char *last)
{
  if (p != last && *p != '/') {
    if (*p == '[') {
      for (p++; p != last && *p != ']'; p++)
	/* nada */;
      if (p != last)
	p++;
    } else
      p++;
  }
}

static int
next_flow_code(const char *&p, const char *last,
	       int port, Bitvector &code, ErrorHandler *errh)
{
  if (p == last || *p == '/') {
    // back up to last code character
    if (p[-1] == ']') {
      for (p -= 2; *p != '['; p--)
	/* nada */;
    } else
      p--;
  }

  code.assign(256, false);

  if (*p == '[') {
    bool negated = false;
    if (p[1] == '^')
      negated = true, p++;
    for (p++; p != last && *p != ']'; p++) {
      if (isalpha(*p))
	code[*p] = true;
      else if (*p == '#')
	code[port + 128] = true;
      else if (errh)
	errh->error("flow code: invalid character `%c'", *p);
    }
    if (negated)
      code.negate();
    if (p == last) {
      if (errh)
	errh->error("flow code: missing `]'");
      p--;			// don't skip over final '\0'
    }
  } else if (isalpha(*p))
    code[*p] = true;
  else if (*p == '#')
    code[port + 128] = true;
  else {
    if (errh)
      errh->error("flow code: invalid character `%c'", *p);
    p++;
    return -1;
  }

  p++;
  return 0;
}

int
ProcessingT::forward_flow(const String &flow_code, int input_port,
			  int noutputs, Bitvector *bv)
{
  if (input_port < 0) {
    bv->assign(noutputs, false);
    return 0;
  } else if (!flow_code || (flow_code.length() == 3 && flow_code == "x/x")) {
    bv->assign(noutputs, true);
    return 0;
  }

  bv->assign(noutputs, false);
  ErrorHandler *errh = ErrorHandler::default_handler();

  int slash_pos = flow_code.find_left('/');
  if (slash_pos <= 0 || slash_pos == flow_code.length() - 1 || flow_code[slash_pos + 1] == '/')
    return errh->error("flow code: missing or bad `/'");

  const char *f_in = flow_code.data();
  const char *f_out = f_in + slash_pos + 1;
  const char *f_last = f_in + flow_code.length();
  
  Bitvector in_code;
  for (int i = 0; i < input_port; i++)
    skip_flow_code(f_in, f_last);
  next_flow_code(f_in, f_last, input_port, in_code, errh);

  Bitvector out_code;
  for (int i = 0; i < noutputs; i++) {
    next_flow_code(f_out, f_last, i, out_code, errh);
    if (in_code.nonzero_intersection(out_code))
      (*bv)[i] = true;
  }

  return 0;
}

int
ProcessingT::backward_flow(const String &flow_code, int output_port,
			   int ninputs, Bitvector *bv)
{
  if (output_port < 0) {
    bv->assign(ninputs, false);
    return 0;
  } else if (!flow_code || (flow_code.length() == 3 && flow_code == "x/x")) {
    bv->assign(ninputs, true);
    return 0;
  }

  bv->assign(ninputs, false);
  ErrorHandler *errh = ErrorHandler::default_handler();

  int slash_pos = flow_code.find_left('/');
  if (slash_pos <= 0 || slash_pos == flow_code.length() - 1 || flow_code[slash_pos + 1] == '/')
    return errh->error("flow code: missing or bad `/'");

  const char *f_in = flow_code.data();
  const char *f_out = f_in + slash_pos + 1;
  const char *f_last = f_in + flow_code.length();
  
  Bitvector out_code;
  for (int i = 0; i < output_port; i++)
    skip_flow_code(f_out, f_last);
  next_flow_code(f_out, f_last, output_port, out_code, errh);

  Bitvector in_code;
  for (int i = 0; i < ninputs; i++) {
    next_flow_code(f_in, f_last, i, in_code, errh);
    if (in_code.nonzero_intersection(out_code))
      (*bv)[i] = true;
  }

  return 0;
}
