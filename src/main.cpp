#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "MPC.h"
#include "json.hpp"

// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.rfind("}]");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

// Evaluate a polynomial.
double polyeval(Eigen::VectorXd coeffs, double x) {
  double result = 0.0;
  for (int i = 0; i < coeffs.size(); i++) {
    result += coeffs[i] * pow(x, i);
  }
  return result;
}

// Fit a polynomial.
// Adapted from
// https://github.com/JuliaMath/Polynomials.jl/blob/master/src/Polynomials.jl#L676-L716
Eigen::VectorXd polyfit(Eigen::VectorXd xvals, Eigen::VectorXd yvals,
                        int order) {
  assert(xvals.size() == yvals.size());
  assert(order >= 1 && order <= xvals.size() - 1);
  Eigen::MatrixXd A(xvals.size(), order + 1);

  for (int i = 0; i < xvals.size(); i++) {
    A(i, 0) = 1.0;
  }

  for (int j = 0; j < xvals.size(); j++) {
    for (int i = 0; i < order; i++) {
      A(j, i + 1) = A(j, i) * xvals(j);
    }
  }

  auto Q = A.householderQr();
  auto result = Q.solve(yvals);
  return result;
}

int main() {
  uWS::Hub h;

  // MPC is initialized here!
  MPC mpc;

  h.onMessage([&mpc](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    string sdata = string(data).substr(0, length);
    cout << sdata << endl;
    if (sdata.size() > 2 && sdata[0] == '4' && sdata[1] == '2') {
      string s = hasData(sdata);
      if (s != "") {
        auto j = json::parse(s);
        string event = j[0].get<string>();
        if (event == "telemetry") {
          // j[1] is the data JSON object
          vector<double> ptsx = j[1]["ptsx"];
          vector<double> ptsy = j[1]["ptsy"];
          double px = j[1]["x"];
          double py = j[1]["y"];
          double psi = j[1]["psi"];
          double v = j[1]["speed"];
          
          // Convert the waypoints to Car's Co-ordinate system for easier calculation of errors
          for(unsigned int i = 0; i < ptsx.size(); i++)
          {
             // Shift origin to Car's origin
             double new_x = ptsx[i] - px;
             double new_y = ptsy[i] - py;
             
             // Rotate by psi degrees
             ptsx[i] = (new_x*cos(0 - psi)) - (new_y*sin(0 - psi));
             ptsy[i] = (new_x*sin(0 - psi)) + (new_y*cos(0 - psi));
          }
          
          // Prepare vector for polyfit
          double* ptr_ptsx = &ptsx[0];
          double* ptr_ptsy = &ptsy[0];
          Eigen::Map<Eigen::VectorXd> ptsx_transformed(ptr_ptsx, 6);
          Eigen::Map<Eigen::VectorXd> ptsy_transformed(ptr_ptsy, 6);
          
          // Fit a polynomial using transformed points
          auto coeffs = polyfit(ptsx_transformed, ptsy_transformed, 3);
          
          // Calculate cte and epsi
          double cte = polyeval(coeffs, 0);
          //double epsi = psi - atan(coeffs[1] + (coeffs[2]*2*px) + (coeffs[3]*3*pow(px,2))); // psi - arctan(f'(x))
          double epsi = -atan(coeffs[1]);

          /*
          * TODO: Calculate steering angle and throttle using MPC.
          *
          * Both are in between [-1, 1].
          *
          */
          double steer_value = j[1]["steering_angle"];
          double throttle_value = j[1]["throttle"];
          
          Eigen::VectorXd state(6);
          double latency_in_sec = 0.1;
          double latency_in_msec = latency_in_sec*1000;
          double Lf = 2.67;
          // In the car co-ordinate syste, the cars x,y points and heading are zero
          double new_px = v*latency_in_sec; // px + v*cos(psi)*dt
          double new_py = 0; // py + v*sin(psi)*dt
          double new_psi = (v/Lf)*(-steer_value)*latency_in_sec; //psi + (v/Lf)*delta*dt
          double new_v = v + (throttle_value*latency_in_sec);
          double new_cte = cte + (v*sin(epsi)*latency_in_sec);
          double new_epsi = epsi - ((-steer_value)*(v/Lf)*latency_in_sec);
          state << new_px, new_py, new_psi, new_v, new_cte, new_epsi;
          
          // Pass in the state and the path(coefficients) to follow to MPC
          auto vars = mpc.Solve(state, coeffs); 

          steer_value = -vars[0]/(deg2rad(25)*Lf);
          throttle_value = vars[1];

          //Display the MPC predicted trajectory 
          vector<double> mpc_x_vals;
          vector<double> mpc_y_vals;

          //.. add (x,y) points to list here, points are in reference to the vehicle's coordinate system
          // the points in the simulator are connected by a Green line
          for (unsigned int i = 0; i < vars.size(); i++)
          {
             if((i%2) == 0)
             {
                mpc_x_vals.push_back(vars[i]);
             }
             else
             {
                mpc_y_vals.push_back(vars[i]);                
             }
          }          
          
          //Display the waypoints/reference line
          vector<double> next_x_vals;
          vector<double> next_y_vals;

          //.. add (x,y) points to list here, points are in reference to the vehicle's coordinate system
          // the points in the simulator are connected by a Yellow line
          double incr_dist = 2.5;
          int num_points = 25;
          for(int i = 0; i < num_points; i++)
          {
             double temp_x = incr_dist*i;
             next_x_vals.push_back(temp_x);
             next_y_vals.push_back(polyeval(coeffs, temp_x));
          }
          

          json msgJson;
          // NOTE: Remember to divide by deg2rad(25) before you send the steering value back.
          // Otherwise the values will be in between [-deg2rad(25), deg2rad(25] instead of [-1, 1].
          msgJson["steering_angle"] = steer_value;
          msgJson["throttle"] = throttle_value;

          msgJson["mpc_x"] = mpc_x_vals;
          msgJson["mpc_y"] = mpc_y_vals;

          msgJson["next_x"] = next_x_vals;
          msgJson["next_y"] = next_y_vals;


          auto msg = "42[\"steer\"," + msgJson.dump() + "]";
          std::cout << msg << std::endl;
          // Latency
          // The purpose is to mimic real driving conditions where
          // the car does actuate the commands instantly.
          //
          // Feel free to play around with this value but should be to drive
          // around the track with 100ms latency.
          //
          // NOTE: REMEMBER TO SET THIS TO 100 MILLISECONDS BEFORE
          // SUBMITTING.
          this_thread::sleep_for(chrono::milliseconds(100));
          ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
