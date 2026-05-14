# ADR-0001 : représentation monétaire en `int64_t` d'unités mineures

- Statut : Accepté
- Date : 2026-05-14

## Contexte

WealthTorii doit manipuler des montants monétaires (transactions bancaires, budgets, allocations, calculs de répartition 50/30/20, futurs investissements). Les `float`/`double` introduisent des erreurs d'arrondi inacceptables pour un outil financier (0.1 + 0.2 != 0.3 en IEEE 754).

## Décision

`wealthtorii::money::Money` stocke le montant comme `std::int64_t minor_units_` représentant la plus petite unité de la devise (centimes pour EUR/USD/CHF). Aucun montant n'est jamais représenté en flottant dans le domaine.

- Format : `Money{12345, Currency::EUR}` représente 123,45 €.
- Plage : `int64_t` couvre ±92 milliards de milliards de centimes — largement suffisant.
- Comparaisons et arithmétique simples sont exactes.

## Conséquences

- Toute valeur d'entrée (CSV, API, saisie) doit être parsée vers `int64_t` minor_units. Cf. `Money::from_string`.
- Les opérations cross-devise nécessitent une étape de conversion explicite (taux de change), jamais d'opération arithmétique directe — d'où `CurrencyMismatch`.
- Les split proportionnels (ex. répartir un revenu en 50/30/20) doivent garantir conservation à l'euro : la somme des parts doit égaler exactement le montant initial, le centime résiduel allant à une part désignée.
- Les pourcentages et taux sont représentés par des rationnels (numérateur/dénominateur) ou des entiers en basis points (1/10000), jamais en `double`.

## Alternatives écartées

- `double` : précision insuffisante, erreurs d'arrondi cumulatives.
- `boost::multiprecision::cpp_dec_float` : surdimensionné pour l'usage, dépendance lourde.
- Type décimal externe (mp-units, etc.) : utile à terme pour le multi-devise et le typage d'unités, mais ajoute de la complexité au stade actuel.
