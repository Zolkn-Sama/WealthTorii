# WealthTorii

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![CMake](https://img.shields.io/badge/CMake-3.28%2B-blue)
![vcpkg](https://img.shields.io/badge/deps-vcpkg-green)
![Tests](https://img.shields.io/badge/tests-GoogleTest-brightgreen)
![Status](https://img.shields.io/badge/status-in%20progress-orange)

WealthTorii est une base de plateforme financière moderne en **C++** orientée :
- suivi de patrimoine,
- modélisation monétaire,
- logique de ledger,
- gestion de portefeuille,
- analytics financiers.

Le projet est pensé comme un socle sérieux, modulaire et testable, capable d’évoluer progressivement d’un usage personnel vers une application plus complète.

---

## Vision

WealthTorii a pour objectif de construire une fondation technique propre pour une application financière capable de gérer :

- des montants et devises de manière sûre,
- des comptes et transactions métier,
- des positions de portefeuille,
- des calculs de performance,
- une future couche API et CLI,
- puis, à terme, une architecture plus ambitieuse.

Le projet privilégie d’abord :
- la **clarté du modèle métier**,
- la **qualité du code**,
- la **testabilité**,
- la **maintenabilité**.

---

## État actuel

Le projet est actuellement en phase de fondation.

Modules en cours :
- `money`
- `ledger`
- `portfolio`

Modules prévus :
- `market_data`
- `analytics`
- `api`
- `cli`

---

## Stack technique

- **C++20**
- **CMake**
- **CMake Presets**
- **vcpkg** pour la gestion des dépendances
- **GoogleTest** pour les tests
- **clang-format** pour le formatage
- **clang-tidy** pour l’analyse statique
