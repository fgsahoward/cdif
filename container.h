#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <type_traits>

#include "cdif.h"


namespace cdif {
    class Container {
        private:
            std::unique_ptr<cdif::Registrar> _registrar;
            std::unique_ptr<cdif::ServiceNameFactory> _serviceNameFactory;
            std::unique_ptr<cdif::PerThreadDependencyChainTracker> _dependencyChain;

            void CheckCircularDependencyResolution(const std::string & name) const {
                auto count = _dependencyChain->Increment(name);
                if (count > 1)
                    throw std::runtime_error("Circular dependecy detected while resolving: " + name);
            }

            template <typename TService>
            TService UnguardedResolve(const std::string & serviceName) const {
                const std::unique_ptr<Registration> & registration = _registrar->GetRegistration<TService>(serviceName);
                return registration->Resolve<TService>(*this);
            }

        public:
            Container() :
                    _registrar(std::move(std::make_unique<cdif::Registrar>())), 
                    _serviceNameFactory(std::move(std::make_unique<cdif::ServiceNameFactory>())),
                    _dependencyChain(std::move(std::make_unique<cdif::PerThreadDependencyChainTracker>()))
                    {};

            virtual ~Container() = default;

            Container(Container && other) : _registrar(std::move(other._registrar)) {};

            Container & operator=(Container && other) {
                if (this != &other)
                    _registrar = std::move(other._registrar);
                return *this;
            }

            template <typename TService, typename TImpl>
            void Register(const std::function<std::shared_ptr<TImpl> (const cdif::Container &)> & resolver, const std::string & name = "") {
                static_assert(std::is_base_of<TService, TImpl>::value, "Implementation must be derived from Service");
                auto serviceResolver = [resolver] (const cdif::Container & ctx) { return static_cast<std::shared_ptr<TService>>(resolver(ctx)); };

                Register<std::shared_ptr<TService>>(serviceResolver, name);
            }

            template <typename TService, typename TImpl, typename ... TDeps>
            void RegisterShared(const std::string & name = "") {
                auto resolver = [] (const cdif::Container & ctx) { return std::make_shared<TImpl>(ctx.Resolve<TDeps>()...); };
                Register<TService, TImpl>(resolver, name);
            }

            template <typename TService>
            void RegisterInstance(const std::shared_ptr<TService> & instance, const std::string & name = "") {
                auto resolver = [instance] (const cdif::Container & ctx) { return instance; };
                Register<std::shared_ptr<TService>>(resolver, name);
            }

            template <typename TService, typename ... TDeps>
            void RegisterInstance(TDeps ... args, const std::string & name = "") {
                auto instance = std::make_shared<TService>(args...);
                RegisterInstance<TService>(instance, name);
            }

            template <typename TService, typename ... TArgs>
            void RegisterFactory(const std::function<TService(TArgs...)> & factory, const std::string & name = "") {
                auto serviceResolver = [factory] (const cdif::Container &) { return factory; };
                Register<std::function<TService(TArgs...)>>(serviceResolver, name);
            }

            template <typename TService>
            void RegisterFactory(const std::function<TService()> & factory, const std::string & name = "") {
                auto serviceResolver = [factory] (const cdif::Container & ctx) { return factory; };
                Register<std::function<TService()>>(serviceResolver, name);
            }

            template <typename TModule>
            void RegisterModule() {
                static_assert(std::is_base_of<IModule, TModule>::value, "Registered modules must derive from IModule.");
                static_assert(std::is_default_constructible<TModule>::value, "Module must have default constructor.");

                auto module = TModule();
                module.Load(*this);
            }

            template <typename TService, typename ... TDeps>
            void Register(const std::string & name = "") {
                auto resolver = [] (const cdif::Container & ctx) { return TService(ctx.Resolve<TDeps>()...); };
                Register<TService>(resolver, name);
            }

            template <typename TService>
            void Register(const std::function<TService (const cdif::Container &)> & serviceResolver, const std::string & name = "") {
                _registrar->Register<TService>(serviceResolver, _serviceNameFactory->Create<TService>(name));
            }

            template <typename TService>
            TService Resolve(const std::string & name = "") const {
                auto serviceName = _serviceNameFactory->Create<TService>(name);
                CheckCircularDependencyResolution(serviceName);
                auto service = UnguardedResolve<TService>(serviceName);
                _dependencyChain->Clear(serviceName);
                return service;
            }
    };
}
