// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements the OAuth flow for the report_client.
// We hard-coded the use of the Google authentication service and registered
// with that service.
//
// First login only:
// -----------------
//
// The first time the user tries to use the report_client application,
// a URL is generated and printed to stdout. The user is asked to visit that URL.
// When visiting that URL, the user is prompted to log in using their Google
// account.
//
// When the Google account service has authenticated the user, the user is
// redirected to localhost:<port>/store_code (port is assigned by the OS)
// The web server at this address runs as a go routine implemented in this file.
// The web server at this address expects a "code" parameter. That "code"
// parameter is a string which can be used to obtain a refresh token.
// The code is then sent to the Google token server which responds with a
// refresh token.
//
// This refresh token is stored on disk at ~/.cobalt_report_client_oauth_token_file.
//
// Subsequent uses:
// ----------------
//
// The refresh token is read from disk. Using that refresh token, a TokenSource
// is created.
//
// That TokenSource is then wrapped in a jwtSource which extracts the JSON Web Token
// from the tokens returned by the TokenSource and passes those on.
//
// This jwtSource is was is used by gRPC to authenticate the user.
package report_client

import (
	"crypto/rand"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"github.com/golang/glog"
	"golang.org/x/net/context"
	"golang.org/x/oauth2"
	"golang.org/x/oauth2/google"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"time"
)

const (
	// The clientId and clientSecret have been registered with the Google
	// authentication service. They are uniquely associated with this application.
	// The clientId must be specified as an allowed audience in
	// kubernetes/report_master/report_master_endpoint.yaml
	clientId = "915138408459-535q0s4l88eppnidvidhlcdvavdcgtfq.apps.googleusercontent.com"
	// In the public client OAuth model the "client secret" is not really a secret
	// and is not used for security. But the Google auth service requires us to send it.
	clientSecret            = "0iEvP5a_yzI1q42c3LMxzKAj"
	refreshTokenPathEnv     = "COBALT_REPORT_CLIENT_OAUTH_TOKEN_FILE"
	refreshTokenPathDefault = ".cobalt_report_client_oauth_token_file"
)

// Returns a TokenSource that vends JWT bearer tokens.
func getTokenSource() oauth2.TokenSource {
	c := getOauthConfig()
	r := getRefreshToken(context.Background(), c)
	s := c.TokenSource(context.Background(), r)
	return jwtSource{s: s}
}

// getOauthConfig returns a pointer to a pre-defined oauth2.Config.
func getOauthConfig() *oauth2.Config {
	return &oauth2.Config{
		ClientID:     clientId,
		ClientSecret: clientSecret,
		Scopes:       []string{"email"},
		Endpoint:     google.Endpoint,
	}
}

// getRefreshToken will try to get the refresh token stored on disk. If no such
// token is to be found, it initiates the authorization flow.
func getRefreshToken(ctx context.Context, c *oauth2.Config) *oauth2.Token {
	// First, we try to get the refresh token from disk.
	t := getRefreshTokenFromFile()
	if t != nil {
		// We force the contained bearer token to expire immediately. This is because
		// the id token is not stored alongside the bearer token and so we will want
		// to refresh the token before using it.
		t.Expiry = time.Unix(0, 0)
		return t
	}

	// If the token could not be gotten from disk, we initiate the authorization flow.
	code := getCodeFromServer(c)
	t = getRefreshTokenFromCode(ctx, c, code)

	// Then, we store the new refresh token on disk for future usage.
	// Refresh tokens do not expire.
	f, err := os.OpenFile(getRefreshTokenFilePath(), os.O_RDWR|os.O_CREATE, 0600)
	if err != nil {
		glog.Fatal(err)
	}
	defer f.Close()
	e := json.NewEncoder(f)
	e.Encode(t)
	return t
}

// getRefreshTokenFromFile tries to read the refresh token stored on disk if
// it can be found.
func getRefreshTokenFromFile() *oauth2.Token {
	path := getRefreshTokenFilePath()
	f, err := os.Open(path)
	if err != nil {
		return nil
	}
	defer f.Close()
	d := json.NewDecoder(f)
	var t oauth2.Token
	if err := d.Decode(&t); err != nil {
		glog.Fatalf("%v: Try deleting %v.", err, path)
	}
	return &t
}

// getRefreshTokenFilePath gets the path at which the refresh token is expected to be stored.
func getRefreshTokenFilePath() (path string) {
	path = os.Getenv(refreshTokenPathEnv)
	if len(path) > 0 {
		return path
	}

	return filepath.Join(os.Getenv("HOME"), refreshTokenPathDefault)
}

// getRefreshTokenFromCode requests an oauth2 token given an authorization code.
// See https://tools.ietf.org/html/rfc6749#section-4.1.3
// We expect the response to this request to include a bearer token, a refresh token
// and an id token.
func getRefreshTokenFromCode(ctx context.Context, c *oauth2.Config, code string) *oauth2.Token {
	t, err := c.Exchange(ctx, code)
	if err != nil {
		glog.Fatal(err)
	}
	return t
}

// getCodeFromServer obtains an oauth2 authorization code from the Google
// Authorization server. See https://tools.ietf.org/html/rfc6749#section-4.1
// subsections 4.1.1 and 4.1.2.
func getCodeFromServer(c *oauth2.Config) string {
	// state is a randomly generated string which is given to the Google
	// authorization service and is passed back to the local web server to check
	// that the user was redirected to the local web server by the Google
	// authorization service.
	// See https://tools.ietf.org/html/rfc6749#section-4.1.1
	stateBytes := make([]byte, 64)
	rand.Read(stateBytes)
	state := base64.StdEncoding.EncodeToString(stateBytes)

	h := &handler{
		state: state,
		c:     make(chan string),
	}

	l, err := net.Listen("tcp", ":0")
	if err != nil {
		glog.Fatal(err)
	}

	listeningAddr, ok := l.Addr().(*net.TCPAddr)
	if !ok {
		glog.Fatal("Starting a local web server did not succeed. Try again.")
	}
	localAddr := fmt.Sprintf("localhost:%v", listeningAddr.Port)

	s := &http.Server{
		Addr:    localAddr,
		Handler: h,
	}

	// We start serving the handler for the authorization response.
	go s.Serve(l)

	c.RedirectURL = fmt.Sprintf("http://%v/store_code", localAddr)
	// We print out for the user the URL they must visit to complete the
	// authorization flow.
	url := c.AuthCodeURL(state, oauth2.AccessTypeOffline)
	fmt.Printf("Visit the URL for the auth dialog: %v\n\n", url)

	// We wait on the handler for the authorization code and shut down the server.
	code := <-h.c
	l.Close()
	return code
}

// handler handles Authorization responses in the form of HTTP requests.
// See https://tools.ietf.org/html/rfc6749#section-4.1.2
type handler struct {
	state string
	c     chan string
}

// ServeHTTP accepts responses from the Google Authorization Server in the form
// of redirection of the user after authentication.
func (h *handler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	// Browsers will try to get a favicon. We want to ignore those requests.
	if r.URL.Path != "/store_code" {
		return
	}

	// We parse the parameters provided by the Google Authorization Server.
	if err := r.ParseForm(); err != nil {
		w.Write([]byte(fmt.Sprintf("%v", err)))
		glog.Fatal(err)
	}

	// state has been passed to the Google Authorization Server and it is supposed
	// to be provided in the parameters. We check that the state parameter is the
	// one we gave to the authorization server.
	state := r.Form["state"][0]
	if state != h.state {
		glog.Fatal("The state string received does not match that sent to the Google Authorization Server.")
	}

	if codes, ok := r.Form["code"]; !ok || len(codes) != 1 {
		glog.Fatal("Authorization code missing from the authorization response.")
	}

	w.Write([]byte("You can close this window now."))

	// Send the code back to the main go routine.
	h.c <- r.Form["code"][0]
}

// jwtSource implements the oauth2.TokenSource interface.
// When a token is requested, jwtSource calls s.Token and replaces the AccessToken
// with an "id_token" found in Token.Extra.
// This forces consumers of a TokenSource to use the JWT which is found in the
// id_token field.
type jwtSource struct {
	// The underlying source of tokens.
	s oauth2.TokenSource
}

func (s jwtSource) Token() (t *oauth2.Token, err error) {
	t, err = s.s.Token()
	if err != nil {
		return nil, err
	}

	if err := toJwt(t); err != nil {
		return nil, err
	}

	return t, nil
}

// toJwt replaces the AccessToken of t with the value of t.Extra("id_token").
// This value is expected to be a JSON Web Token.
func toJwt(t *oauth2.Token) error {
	if t.Extra("id_token") == nil {
		return fmt.Errorf("OAuth login failed: No id_token found in Extra.")
	}

	t.AccessToken = t.Extra("id_token").(string)
	return nil
}
