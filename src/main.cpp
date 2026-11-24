#include "app/Application.hpp"

int main(int argc, char **argv) {
  launcher::Application application;
  return application.run(argc, argv);
}
